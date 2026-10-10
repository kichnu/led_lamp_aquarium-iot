#include "espnow_sync.h"
#include "../config/config.h"
#include "../core/logging.h"
#include "../core/lamp_lock.h"
#include "../hardware/rtc_controller.h"
#include "../hardware/fram_constants.h"
#include "../lamp/lamp_storage.h"
#include "../lamp/program_store.h"
#include "../lamp/light_engine.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <mbedtls/md.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define FRAME_MAGIC         0x4E52      // "RN"
#define FRAME_VERSION       1
#define FRAME_MAX           250         // ESP-NOW v1 — działa z każdym firmware/IDF
#define TAG_LEN             16          // HMAC-SHA256 skrócony do 128 bit
#define KEY_CONTEXT         "RL90-ESPNOW-v1:"

#define HB_INTERVAL_MS      15000
#define HB_DEBOUNCE_MS      300         // zmiana katalogu/aktywnego → heartbeat zaraz
#define CHANGE_CHECK_MS     500
#define PEER_ONLINE_MS      45000       // 3 heartbeaty
#define PEER_FORGET_MS      3600000UL
#define LIST_REQ_MIN_MS     20000       // różny skrót → prośba o listy najwyżej co 20 s
#define LIST_IDS            24
#define CHUNK_DATA          192         // ≤ 200 B (§9.4)
#define CHUNK_TIMEOUT_MS    400
#define CHUNK_RETRIES       5
#define FETCH_QUEUE_LEN     FRAM_PROGRAM_SLOTS
#define ACT_RETRY_MS        500
#define ACT_RETRIES         6
#define TIME_SET_MIN_MS     600000UL    // korekta czasu z innej lampy najwyżej co 10 min
#define TIME_SET_TOL_S      2
#define TIME_VALID_MIN      1700000000UL

enum FrameType : uint8_t {
    FT_HB = 1,          // broadcast: stan lampy, skrót katalogu, czas
    FT_LIST_REQ,        // prośba o listy id programów i tombstone
    FT_LIST,            // strona listy (kind 0 = programy, 1 = tombstone)
    FT_PROG_REQ,        // prośba o porcję programu
    FT_PROG_DATA,       // porcja programu (total = 0: brak programu)
    FT_ACTIVATE,        // „ustaw na wszystkich”
    FT_ACK,             // potwierdzenie FT_ACTIVATE
};

enum AckStatus : uint8_t { ACK_ACTIVATED = 0, ACK_FETCHING, ACK_REJECTED };

#pragma pack(push, 1)
struct FrameHdr {
    uint16_t magic;
    uint8_t  version;
    uint8_t  type;
    uint32_t boot_id;           // losowy przy każdym starcie
    uint32_t seq;               // rośnie w obrębie boot_id — odrzucanie powtórek
};
struct HbBody {
    char     name[32];
    uint32_t ip;
    uint64_t active_id;
    char     active_name[PROGRAM_NAME_LEN];
    uint32_t catalog_hash;
    uint32_t utc;
    uint32_t programs_created;
    uint8_t  prog_count;
    uint8_t  mode;
    uint8_t  time_q;
    uint8_t  flags;             // bit0: aktywny osierocony
    char     fw[12];
};
struct ListBody {
    uint8_t  kind;
    uint8_t  page;
    uint8_t  pages;
    uint8_t  n;
    uint64_t ids[LIST_IDS];     // wysyłane tylko n
};
struct ProgReqBody {
    uint64_t id;
    uint8_t  chunk;
};
struct ProgDataBody {
    uint64_t id;
    uint16_t total;
    uint8_t  chunk;
    uint8_t  chunks;
    uint8_t  len;
    uint8_t  data[CHUNK_DATA];  // wysyłane tylko len
};
struct ActivateBody {
    uint64_t id;
    uint32_t cmd_id;
};
struct AckBody {
    uint32_t cmd_id;
    uint8_t  status;
};
#pragma pack(pop)

static_assert(sizeof(FrameHdr) + sizeof(HbBody) + TAG_LEN <= FRAME_MAX, "HB za duży");
static_assert(sizeof(FrameHdr) + sizeof(ListBody) + TAG_LEN <= FRAME_MAX, "LIST za duży");
static_assert(sizeof(FrameHdr) + sizeof(ProgDataBody) + TAG_LEN <= FRAME_MAX, "PROG_DATA za duży");
static_assert((PROGRAM_BLOB_MAX + CHUNK_DATA - 1) / CHUNK_DATA <= 255, "za dużo porcji");

struct RxItem {
    uint8_t mac[6];
    uint8_t len;
    uint8_t data[FRAME_MAX];
};

struct Peer {
    bool     used;
    uint8_t  mac[6];
    uint32_t boot_id;
    uint32_t last_seq;
    uint32_t last_seen_ms;
    uint32_t last_list_req_ms;
    bool     have_hb;
    HbBody   hb;
    uint32_t last_cmd_id;       // ostatnie FT_ACTIVATE od tej lampy (powtórzenie = tylko ACK)
};

struct FetchItem {
    uint64_t id;
    uint8_t  mac[6];
};

struct ActCmd {
    bool     used;
    uint8_t  mac[6];
    uint64_t id;
    uint32_t cmd_id;
    uint32_t last_ms;
    uint8_t  tries;
};

static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static bool          s_enabled = false;
static uint8_t       s_key[32];
static uint32_t      s_bootId = 0;
static uint32_t      s_txSeq = 0;
static QueueHandle_t s_rxQueue = nullptr;
static Peer          s_peers[ESPNOW_MAX_PEERS];
static uint32_t      s_rxOk = 0, s_rxBad = 0;
static uint32_t      s_txFail = 0;         // błąd esp_now_send (loop)
static volatile uint32_t s_txNoAck = 0;    // unicast bez ACK MAC (task Wi-Fi)

static uint32_t s_lastHbMs = 0;
static uint32_t s_hbDueMs = 0;          // 0 = brak zaplanowanego
static uint32_t s_lastChangeCheckMs = 0;
static uint32_t s_lastHash = 0;
static uint64_t s_lastActiveId = 0;
static bool     s_lastOrphan = false;
static uint32_t s_lastTimeSetMs = 0;
static bool     s_timeSetOnce = false;

static FetchItem s_fetchQ[FETCH_QUEUE_LEN];
static uint8_t   s_fetchCount = 0;
static uint64_t  s_pendingActivate = 0;

static struct {
    bool     active;
    uint64_t id;
    uint8_t  mac[6];
    uint8_t  next;
    uint8_t  chunks;
    uint16_t total;
    uint32_t req_ms;
    uint8_t  retries;
} s_xfer;
static uint8_t s_rxBlob[PROGRAM_BLOB_MAX];

static uint64_t s_txBlobId = 0;          // cache programu wysyłanego porcjami
static uint16_t s_txBlobLen = 0;
static uint8_t  s_txBlob[PROGRAM_BLOB_MAX];

static ActCmd   s_act[ESPNOW_MAX_PEERS];
static uint32_t s_nextCmdId = 1;

// ===============================
// Pomocnicze
// ===============================

static bool macEq(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }

static void hexId(uint64_t id, char* out) {   // 17 B
    snprintf(out, 17, "%08lX%08lX", (unsigned long)(id >> 32), (unsigned long)(id & 0xFFFFFFFF));
}

static void hmacTag(const uint8_t* data, size_t len, uint8_t* tag) {
    uint8_t full[32];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), s_key, sizeof(s_key), data, len, full);
    memcpy(tag, full, TAG_LEN);
}

static bool tagOk(const uint8_t* data, size_t len, const uint8_t* tag) {
    uint8_t expect[TAG_LEN];
    hmacTag(data, len, expect);
    uint8_t diff = 0;
    for (int i = 0; i < TAG_LEN; i++) diff |= expect[i] ^ tag[i];
    return diff == 0;
}

static Peer* findPeer(const uint8_t* mac) {
    for (auto& p : s_peers) if (p.used && macEq(p.mac, mac)) return &p;
    return nullptr;
}

static Peer* addPeer(const uint8_t* mac) {
    Peer* slot = nullptr;
    for (auto& p : s_peers) if (!p.used) { slot = &p; break; }
    if (!slot) {   // pełno — najdłużej milcząca
        slot = &s_peers[0];
        for (auto& p : s_peers) if (millis() - p.last_seen_ms > millis() - slot->last_seen_ms) slot = &p;
        esp_now_del_peer(slot->mac);
    }
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    memcpy(slot->mac, mac, 6);

    esp_now_peer_info_t info = {};
    memcpy(info.peer_addr, mac, 6);
    info.channel = 0;           // bieżący kanał (Wi-Fi)
    info.ifidx = WIFI_IF_STA;
    info.encrypt = false;       // autentyczność z HMAC; treść nie jest tajna
    if (!esp_now_is_peer_exist(mac)) esp_now_add_peer(&info);
    LOG_INFO("ESP-NOW: nowa lampa %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return slot;
}

static bool peerOnline(const Peer& p) {
    return p.used && p.have_hb && millis() - p.last_seen_ms < PEER_ONLINE_MS;
}

// ===============================
// Wysyłanie
// ===============================

static bool sendFrame(const uint8_t* mac, FrameType type, const void* body, size_t bodyLen) {
    if (!s_enabled || WiFi.status() != WL_CONNECTED) return false;   // bez połączenia kanał nieustalony
    uint8_t buf[FRAME_MAX];
    FrameHdr h = { FRAME_MAGIC, FRAME_VERSION, (uint8_t)type, s_bootId, ++s_txSeq };
    size_t n = sizeof(h) + bodyLen;
    if (n + TAG_LEN > sizeof(buf)) return false;
    memcpy(buf, &h, sizeof(h));
    if (bodyLen) memcpy(buf + sizeof(h), body, bodyLen);
    hmacTag(buf, n, buf + n);
    n += TAG_LEN;

    esp_err_t err = esp_now_send(mac, buf, n);
    if (err == ESP_ERR_ESPNOW_NO_MEM) {   // kolejka TX pełna (seria porcji/list) — chwila i jeszcze raz
        delay(5);
        err = esp_now_send(mac, buf, n);
    }
    if (err != ESP_OK) {
        s_txFail++;
        return false;
    }
    return true;
}

static void sendHeartbeat() {
    HbBody b;
    memset(&b, 0, sizeof(b));
    strncpy(b.name, getDeviceID(), sizeof(b.name) - 1);
    b.ip = (uint32_t)WiFi.localIP();
    {
        LampLock lock;
        const Program& p = activeProgram();
        b.active_id = p.id;
        memcpy(b.active_name, p.name, PROGRAM_NAME_LEN);
        b.active_name[PROGRAM_NAME_LEN - 1] = '\0';
        b.programs_created = sysState().programs_created;
        b.prog_count = catalogCount();
    }
    b.catalog_hash = s_lastHash;
    b.flags = s_lastOrphan ? 1 : 0;
    LampStatus st;
    getLampStatus(st);
    b.mode = st.mode;
    b.time_q = getTimeQuality();
    b.utc = isTimeValid() ? (uint32_t)time(nullptr) : 0;
    strncpy(b.fw, FW_VERSION, sizeof(b.fw) - 1);
    sendFrame(BROADCAST, FT_HB, &b, sizeof(b));
    s_lastHbMs = millis();
    s_hbDueMs = 0;
}

static void scheduleHeartbeat() {
    if (!s_hbDueMs) s_hbDueMs = millis() + HB_DEBOUNCE_MS;
    if (!s_hbDueMs) s_hbDueMs = 1;
}

static void sendLists(const uint8_t* mac) {
    ListBody b;
    memset(&b, 0, sizeof(b));
    b.kind = 0;
    b.pages = 1;
    b.n = liveProgramIds(b.ids, LIST_IDS);
    sendFrame(mac, FT_LIST, &b, 4 + b.n * sizeof(uint64_t));

    uint16_t count = tombstoneCount();
    if (count == 0) return;
    uint8_t pages = (count + LIST_IDS - 1) / LIST_IDS;
    for (uint8_t pg = 0; pg < pages; pg++) {
        memset(&b, 0, sizeof(b));
        b.kind = 1;
        b.page = pg;
        b.pages = pages;
        for (uint16_t i = pg * LIST_IDS; i < count && b.n < LIST_IDS; i++) b.ids[b.n++] = tombstoneAt(i);
        sendFrame(mac, FT_LIST, &b, 4 + b.n * sizeof(uint64_t));
    }
}

static void sendProgChunk(const uint8_t* mac, uint64_t id, uint8_t chunk) {
    ProgDataBody b;
    memset(&b, 0, sizeof(b));
    b.id = id;
    if (s_txBlobId != id || isTombstoned(id)) {   // skasowany od czasu odczytu — nie wysyłać
        s_txBlobId = 0;
        if (readProgramBlob(id, s_txBlob, s_txBlobLen)) s_txBlobId = id;
    }
    if (s_txBlobId == id) {
        b.total = s_txBlobLen;
        b.chunks = (s_txBlobLen + CHUNK_DATA - 1) / CHUNK_DATA;
        if (chunk < b.chunks) {
            uint16_t off = chunk * CHUNK_DATA;
            b.chunk = chunk;
            b.len = (s_txBlobLen - off > CHUNK_DATA) ? CHUNK_DATA : s_txBlobLen - off;
            memcpy(b.data, s_txBlob + off, b.len);
        } else {
            b.total = 0;
        }
    }
    sendFrame(mac, FT_PROG_DATA, &b, offsetof(ProgDataBody, data) + b.len);
}

// ===============================
// Pobieranie programów
// ===============================

static bool queuedOrFetching(uint64_t id) {
    if (s_xfer.active && s_xfer.id == id) return true;
    for (uint8_t i = 0; i < s_fetchCount; i++) if (s_fetchQ[i].id == id) return true;
    return false;
}

static void enqueueFetch(uint64_t id, const uint8_t* mac, bool front) {
    if (queuedOrFetching(id) || s_fetchCount >= FETCH_QUEUE_LEN) return;
    if (front) {
        memmove(&s_fetchQ[1], &s_fetchQ[0], s_fetchCount * sizeof(FetchItem));
        s_fetchQ[0].id = id;
        memcpy(s_fetchQ[0].mac, mac, 6);
    } else {
        s_fetchQ[s_fetchCount].id = id;
        memcpy(s_fetchQ[s_fetchCount].mac, mac, 6);
    }
    s_fetchCount++;
}

static void dropFromQueue(uint64_t id) {
    for (uint8_t i = 0; i < s_fetchCount; i++) {
        if (s_fetchQ[i].id != id) continue;
        memmove(&s_fetchQ[i], &s_fetchQ[i + 1], (s_fetchCount - i - 1) * sizeof(FetchItem));
        s_fetchCount--;
        return;
    }
}

static void requestChunk() {
    ProgReqBody b = { s_xfer.id, s_xfer.next };
    sendFrame(s_xfer.mac, FT_PROG_REQ, &b, sizeof(b));
    s_xfer.req_ms = millis();
}

static void abortTransfer(const char* why) {
    char hex[17];
    hexId(s_xfer.id, hex);
    LOG_WARNING("ESP-NOW: pobieranie %s przerwane (%s)", hex, why);
    if (s_pendingActivate == s_xfer.id) s_pendingActivate = 0;
    s_xfer.active = false;
}

static void startNextFetch() {
    while (!s_xfer.active && s_fetchCount > 0) {
        FetchItem it = s_fetchQ[0];
        dropFromQueue(it.id);
        if (findProgram(it.id) || isTombstoned(it.id)) continue;
        if (catalogCount() >= FRAM_PROGRAM_SLOTS) {
            LOG_WARNING("ESP-NOW: biblioteka pełna — program z innej lampy pominięty");
            s_fetchCount = 0;
            return;
        }
        memset(&s_xfer, 0, sizeof(s_xfer));
        s_xfer.active = true;
        s_xfer.id = it.id;
        memcpy(s_xfer.mac, it.mac, 6);
        requestChunk();
    }
}

static void finishTransfer() {
    s_xfer.active = false;
    ProgramError e = importProgramBlob(s_rxBlob, s_xfer.total);
    char hex[17];
    hexId(s_xfer.id, hex);
    if (e != PROG_OK) {
        LOG_WARNING("ESP-NOW: program %s odrzucony (%s)", hex, programErrorStr(e));
        if (s_pendingActivate == s_xfer.id) s_pendingActivate = 0;
        return;
    }
    if (s_pendingActivate == s_xfer.id) {
        s_pendingActivate = 0;
        if (activateProgram(s_xfer.id) == PROG_OK) onActiveProgramChanged();
    }
    adoptRenamedOrphan();
}

static void onProgData(const Peer& from, const ProgDataBody& b, size_t len) {
    if (!s_xfer.active || b.id != s_xfer.id || !macEq(from.mac, s_xfer.mac)) return;
    if (b.total == 0) { abortTransfer("brak programu u nadawcy"); return; }
    if (b.chunk != s_xfer.next) return;                    // spóźniona powtórka
    if (b.chunk == 0) {
        if (b.total > PROGRAM_BLOB_MAX || b.chunks != (b.total + CHUNK_DATA - 1) / CHUNK_DATA) {
            abortTransfer("zły rozmiar");
            return;
        }
        s_xfer.total = b.total;
        s_xfer.chunks = b.chunks;
    } else if (b.total != s_xfer.total || b.chunks != s_xfer.chunks) {
        abortTransfer("zmiana rozmiaru");
        return;
    }
    uint16_t off = b.chunk * CHUNK_DATA;
    uint16_t expect = (s_xfer.total - off > CHUNK_DATA) ? CHUNK_DATA : s_xfer.total - off;
    if (b.len != expect || len < offsetof(ProgDataBody, data) + b.len) { abortTransfer("zła porcja"); return; }
    memcpy(s_rxBlob + off, b.data, b.len);
    s_xfer.retries = 0;
    if (++s_xfer.next >= s_xfer.chunks) finishTransfer();
    else requestChunk();
}

// ===============================
// Odbiór
// ===============================

static void onHeartbeat(Peer& p, const HbBody& b) {
    bool first = !p.have_hb;
    p.hb = b;
    p.hb.name[sizeof(p.hb.name) - 1] = '\0';
    p.hb.active_name[PROGRAM_NAME_LEN - 1] = '\0';
    p.hb.fw[sizeof(p.hb.fw) - 1] = '\0';
    p.have_hb = true;
    if (first) {
        LOG_INFO("ESP-NOW: lampa %s (%s), aktywny: %s", p.hb.name, IPAddress(b.ip).toString().c_str(), p.hb.active_name);
        scheduleHeartbeat();   // niech nowa lampa od razu zobaczy nas
    }

    // Licznik nazw „Program NNNN” — max(własny, cudzy)
    if (b.programs_created > sysState().programs_created) {
        sysState().programs_created = b.programs_created;
        saveSystemState();
    }

    // Czas: lampa bez świeżego NTP bierze UTC od lampy, która go ma
    if (b.time_q == 2 && getTimeQuality() < 2 && b.utc > TIME_VALID_MIN) {
        int32_t delta = (int32_t)(b.utc - (uint32_t)time(nullptr));
        bool rateOk = !s_timeSetOnce || millis() - s_lastTimeSetMs >= TIME_SET_MIN_MS;
        if (!isTimeValid() || (abs(delta) >= TIME_SET_TOL_S && rateOk)) {
            LOG_INFO("ESP-NOW: korekta czasu o %ld s od %s", (long)delta, p.hb.name);
            setTimeFromPeer(b.utc);
            s_lastTimeSetMs = millis();
            s_timeSetOnce = true;
        }
    }

    // Różny skrót katalogu → prośba o listy (każda strona sama dociąga, czego jej brakuje)
    if (b.catalog_hash != s_lastHash && !s_xfer.active && s_fetchCount == 0 &&
        (p.last_list_req_ms == 0 || millis() - p.last_list_req_ms >= LIST_REQ_MIN_MS)) {
        p.last_list_req_ms = millis();
        if (!p.last_list_req_ms) p.last_list_req_ms = 1;
        sendFrame(p.mac, FT_LIST_REQ, nullptr, 0);
    }
}

static void onList(const Peer& p, const ListBody& b, size_t len) {
    if (b.n > LIST_IDS || len < 4 + b.n * sizeof(uint64_t)) return;
    if (b.kind == 0) {
        for (uint8_t i = 0; i < b.n; i++) {
            uint64_t id = b.ids[i];
            if (!findProgram(id) && !isTombstoned(id)) enqueueFetch(id, p.mac, false);
        }
    } else if (b.kind == 1) {
        bool changed = false;
        for (uint8_t i = 0; i < b.n; i++) {
            dropFromQueue(b.ids[i]);
            if (applyRemoteTombstone(b.ids[i])) changed = true;
        }
        if (changed) adoptRenamedOrphan();
    }
}

static void onActivate(Peer& p, const ActivateBody& b) {
    AckBody ack = { b.cmd_id, ACK_ACTIVATED };
    if (b.cmd_id != p.last_cmd_id) {
        p.last_cmd_id = b.cmd_id;
        char hex[17];
        hexId(b.id, hex);
        if (isTombstoned(b.id) && b.id != activeProgram().id) {
            ack.status = ACK_REJECTED;
        } else if (findProgram(b.id)) {
            s_pendingActivate = 0;
            if (activeProgram().id != b.id && activateProgram(b.id) == PROG_OK) onActiveProgramChanged();
            LOG_INFO("ESP-NOW: „ustaw na wszystkich” od %s — %s", p.hb.name, activeProgram().name);
        } else {
            ack.status = ACK_FETCHING;
            s_pendingActivate = b.id;
            enqueueFetch(b.id, p.mac, true);
            LOG_INFO("ESP-NOW: „ustaw na wszystkich” od %s — pobieram %s", p.hb.name, hex);
        }
    } else {
        ack.status = (s_pendingActivate == b.id) ? ACK_FETCHING : ACK_ACTIVATED;
    }
    sendFrame(p.mac, FT_ACK, &ack, sizeof(ack));
}

static void onAck(const Peer& p, const AckBody& b) {
    for (auto& a : s_act) {
        if (!a.used || a.cmd_id != b.cmd_id || !macEq(a.mac, p.mac)) continue;
        a.used = false;
        if (b.status == ACK_REJECTED) LOG_WARNING("ESP-NOW: %s odrzuciła program (skasowany)", p.hb.name);
    }
}

static void handleFrame(const RxItem& it) {
    if (it.len < sizeof(FrameHdr) + TAG_LEN) { s_rxBad++; return; }
    size_t n = it.len - TAG_LEN;
    FrameHdr h;
    memcpy(&h, it.data, sizeof(h));
    if (h.magic != FRAME_MAGIC || h.version != FRAME_VERSION || !tagOk(it.data, n, it.data + n)) {
        s_rxBad++;
        return;
    }

    Peer* p = findPeer(it.mac);
    if (p && p->boot_id == h.boot_id && h.seq <= p->last_seq) { s_rxBad++; return; }   // powtórka
    if (!p) p = addPeer(it.mac);
    p->boot_id = h.boot_id;
    p->last_seq = h.seq;
    p->last_seen_ms = millis();
    s_rxOk++;

    const uint8_t* body = it.data + sizeof(h);
    size_t blen = n - sizeof(h);
    switch (h.type) {
        case FT_HB:
            if (blen >= sizeof(HbBody)) { HbBody b; memcpy(&b, body, sizeof(b)); onHeartbeat(*p, b); }
            break;
        case FT_LIST_REQ:
            sendLists(p->mac);
            break;
        case FT_LIST:
            if (blen >= 4) { ListBody b = {}; memcpy(&b, body, min(blen, sizeof(b))); onList(*p, b, blen); }
            break;
        case FT_PROG_REQ:
            if (blen >= sizeof(ProgReqBody)) { ProgReqBody b; memcpy(&b, body, sizeof(b)); sendProgChunk(p->mac, b.id, b.chunk); }
            break;
        case FT_PROG_DATA:
            if (blen >= offsetof(ProgDataBody, data)) { ProgDataBody b = {}; memcpy(&b, body, min(blen, sizeof(b))); onProgData(*p, b, blen); }
            break;
        case FT_ACTIVATE:
            if (blen >= sizeof(ActivateBody) && p->have_hb) { ActivateBody b; memcpy(&b, body, sizeof(b)); onActivate(*p, b); }
            break;
        case FT_ACK:
            if (blen >= sizeof(AckBody)) { AckBody b; memcpy(&b, body, sizeof(b)); onAck(*p, b); }
            break;
        default:
            break;
    }
}

// Task Wi-Fi: tylko kopia do kolejki
static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (!s_rxQueue || len <= 0 || len > FRAME_MAX) return;
    RxItem it;
    memcpy(it.mac, info->src_addr, 6);
    it.len = (uint8_t)len;
    memcpy(it.data, data, len);
    xQueueSend(s_rxQueue, &it, 0);
}

static void onSent(const uint8_t* mac, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS && memcmp(mac, BROADCAST, 6) != 0) s_txNoAck = s_txNoAck + 1;
}

// ===============================
// API
// ===============================

void initEspNow() {
    const char* pass = getWiFiPassword();
    if (!pass || !pass[0]) {
        LOG_WARNING("ESP-NOW wyłączony — brak hasła Wi-Fi (klucz grupy)");
        return;
    }
    // Klucz grupy = SHA-256(kontekst + hasło Wi-Fi) — provisioning bez zmian (USTALENIA.md)
    {
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
        mbedtls_md_starts(&ctx);
        mbedtls_md_update(&ctx, (const uint8_t*)KEY_CONTEXT, strlen(KEY_CONTEXT));
        mbedtls_md_update(&ctx, (const uint8_t*)pass, strlen(pass));
        mbedtls_md_finish(&ctx, s_key);
        mbedtls_md_free(&ctx);
    }

    WiFi.setSleep(false);       // modem sleep gubi ramki ESP-NOW między beaconami
    if (esp_now_init() != ESP_OK) {
        LOG_ERROR("ESP-NOW: esp_now_init nieudany");
        return;
    }
    s_rxQueue = xQueueCreate(12, sizeof(RxItem));
    esp_now_register_recv_cb(onRecv);
    esp_now_register_send_cb(onSent);

    esp_now_peer_info_t info = {};
    memcpy(info.peer_addr, BROADCAST, 6);
    info.channel = 0;
    info.ifidx = WIFI_IF_STA;
    info.encrypt = false;
    esp_now_add_peer(&info);

    s_bootId = esp_random();
    s_enabled = true;
    LOG_INFO("ESP-NOW gotowy (MAC %s)", WiFi.macAddress().c_str());
}

void updateEspNow() {
    if (!s_enabled) return;
    LampLock lock;
    RxItem it;
    for (int i = 0; i < 6 && xQueueReceive(s_rxQueue, &it, 0) == pdTRUE; i++) handleFrame(it);

    // Po odbiorze: handleFrame() ustawia last_seen_ms / req_ms / last_ms na millis() — wcześniejsze
    // now dawałoby now − t < 0 → przekręcenie uint32 (lampa zapominana od razu, porcje ponawiane bez czekania)
    uint32_t now = millis();

    // Zmiana katalogu / aktywnego (GUI, sync) → heartbeat zaraz
    if (now - s_lastChangeCheckMs >= CHANGE_CHECK_MS) {
        s_lastChangeCheckMs = now;
        uint32_t hash = catalogHash();
        uint64_t act = activeProgram().id;
        bool orphan = activeIsOrphan();
        if (hash != s_lastHash || act != s_lastActiveId || orphan != s_lastOrphan) {
            s_lastHash = hash;
            s_lastActiveId = act;
            s_lastOrphan = orphan;
            scheduleHeartbeat();
        }
    }
    if ((s_hbDueMs && (int32_t)(now - s_hbDueMs) >= 0) || now - s_lastHbMs >= HB_INTERVAL_MS) sendHeartbeat();

    // Transfer: ponowienie porcji / następny z kolejki
    if (s_xfer.active && now - s_xfer.req_ms >= CHUNK_TIMEOUT_MS) {
        if (++s_xfer.retries > CHUNK_RETRIES) abortTransfer("timeout");
        else requestChunk();
    }
    startNextFetch();

    // „Ustaw na wszystkich” — ponawianie do ACK
    for (auto& a : s_act) {
        if (!a.used || now - a.last_ms < ACT_RETRY_MS) continue;
        if (a.tries >= ACT_RETRIES) {
            a.used = false;
            Peer* p = findPeer(a.mac);
            LOG_WARNING("ESP-NOW: brak potwierdzenia „ustaw na wszystkich” od %s", p ? p->hb.name : "?");
            continue;
        }
        ActivateBody b = { a.id, a.cmd_id };
        sendFrame(a.mac, FT_ACTIVATE, &b, sizeof(b));
        a.last_ms = now;
        a.tries++;
    }

    // Zapominanie lamp milczących od godziny
    for (auto& p : s_peers) {
        if (p.used && now - p.last_seen_ms > PEER_FORGET_MS) {
            esp_now_del_peer(p.mac);
            p.used = false;
        }
    }
}

uint8_t espnowPeerCount() {
    LampLock lock;
    uint8_t n = 0;
    for (auto& p : s_peers) if (p.used && p.have_hb) n++;
    return n;
}

bool espnowPeer(uint8_t i, PeerInfo& out) {
    LampLock lock;
    for (auto& p : s_peers) {
        if (!p.used || !p.have_hb) continue;
        if (i--) continue;
        memset(&out, 0, sizeof(out));
        memcpy(out.mac, p.mac, 6);
        strncpy(out.name, p.hb.name, sizeof(out.name) - 1);
        out.ip = p.hb.ip;
        out.online = peerOnline(p);
        out.last_seen_s = (millis() - p.last_seen_ms) / 1000;
        out.active_id = p.hb.active_id;
        memcpy(out.active_name, p.hb.active_name, sizeof(out.active_name));
        out.active_orphan = p.hb.flags & 1;
        out.mode = p.hb.mode;
        out.time_quality = p.hb.time_q;
        out.in_sync = p.hb.catalog_hash == s_lastHash;
        out.prog_count = p.hb.prog_count;
        strncpy(out.fw, p.hb.fw, sizeof(out.fw) - 1);
        return true;
    }
    return false;
}

void espnowStatus(EspNowStatus& out) {
    LampLock lock;
    memset(&out, 0, sizeof(out));
    out.enabled = s_enabled;
    WiFi.macAddress(out.mac);
    out.channel = WiFi.channel();
    out.catalog_hash = s_lastHash;
    out.fetch_queue = s_fetchCount;
    out.fetching_id = s_xfer.active ? s_xfer.id : 0;
    out.pending_activate = s_pendingActivate;
    out.rx_ok = s_rxOk;
    out.rx_bad = s_rxBad;
    out.tx_fail = s_txFail + s_txNoAck;
}

bool espnowActiveOnPeer(uint64_t id, char* name, size_t nameLen) {
    LampLock lock;
    for (auto& p : s_peers) {
        if (!peerOnline(p) || p.hb.active_id != id) continue;
        if (name && nameLen) { strncpy(name, p.hb.name, nameLen - 1); name[nameLen - 1] = '\0'; }
        return true;
    }
    return false;
}

uint8_t espnowActivateAll(uint64_t id) {
    if (!s_enabled) return 0;
    LampLock lock;
    uint8_t n = 0;
    for (auto& p : s_peers) {
        if (!peerOnline(p)) continue;
        ActCmd* slot = nullptr;
        for (auto& a : s_act) if (a.used && macEq(a.mac, p.mac)) { slot = &a; break; }   // nowsze zastępuje
        if (!slot) for (auto& a : s_act) if (!a.used) { slot = &a; break; }
        if (!slot) continue;
        slot->used = true;
        memcpy(slot->mac, p.mac, 6);
        slot->id = id;
        slot->cmd_id = s_nextCmdId++ ^ (s_bootId & 0xFFFF0000);
        slot->tries = 0;
        slot->last_ms = millis() - ACT_RETRY_MS;   // wysyłka w najbliższym updateEspNow()
        n++;
    }
    return n;
}
