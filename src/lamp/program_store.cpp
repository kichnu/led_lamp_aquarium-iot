#include "program_store.h"
#include "lamp_storage.h"
#include "../hardware/fram_controller.h"
#include "../core/logging.h"
#include "../core/lamp_lock.h"
#include <WiFi.h>
#include <time.h>

static_assert(sizeof(ProgramHeader) == 64, "ProgramHeader size");
static_assert(sizeof(ProgramHeader) + NUM_CHANNELS * (1 + MAX_POINTS * 4) <= FRAM_PROGRAM_SLOT_SIZE,
              "program nie mieści się w slocie");
static_assert(FRAM_ADDR_PROGRAMS + FRAM_PROGRAM_SLOTS * FRAM_PROGRAM_SLOT_SIZE <= FRAM_SIZE, "sloty poza FRAM");

#define PAYLOAD_MAX (NUM_CHANNELS * (1 + MAX_POINTS * 4))

static CatalogEntry s_catalog[FRAM_PROGRAM_SLOTS];
static uint8_t      s_count = 0;
static Program      s_active;

// ===============================
// Program fabryczny (USTALENIA.md): 10:00–11:00 narasta, 11:00–21:00 stała, 21:00–22:00 opada
// ===============================

static void buildFactoryProgram(Program& p) {
    static const uint16_t LEVEL[NUM_CHANNELS] = { 4000, 7000, 10000, 9000 };  // A 40 %, B 70 %, C 100 %, D 90 %
    memset(&p, 0, sizeof(p));
    p.id = FACTORY_PROGRAM_ID;
    p.flags = PROGRAM_FLAG_FACTORY;
    strncpy(p.name, "Fabryczny", PROGRAM_NAME_LEN - 1);
    for (int c = 0; c < NUM_CHANNELS; c++) {
        const CurvePoint pts[] = { {0, 0}, {600, 0}, {660, LEVEL[c]}, {1260, LEVEL[c]}, {1320, 0}, {DAY_MIN, 0} };
        p.ch[c].count = sizeof(pts) / sizeof(pts[0]);
        memcpy(p.ch[c].pts, pts, sizeof(pts));
    }
}

// ===============================
// Walidacja / ewaluacja
// ===============================

bool validateCurve(const ChannelCurve& c) {
    if (c.count < 2 || c.count > MAX_POINTS) return false;
    if (c.pts[0].t != 0 || c.pts[c.count - 1].t != DAY_MIN) return false;
    for (uint8_t i = 0; i < c.count; i++) {
        if (c.pts[i].v > V_MAX) return false;
        if (i > 0 && c.pts[i].t <= c.pts[i - 1].t) return false;
    }
    return true;
}

float evalCurve(const ChannelCurve& c, float t) {
    if (c.count == 0) return 0;
    if (t <= c.pts[0].t) return c.pts[0].v;
    if (t >= c.pts[c.count - 1].t) return c.pts[c.count - 1].v;
    uint8_t hi = 1;
    while (hi < c.count - 1 && c.pts[hi].t <= t) hi++;
    const CurvePoint& a = c.pts[hi - 1];
    const CurvePoint& b = c.pts[hi];
    return a.v + ((float)b.v - a.v) * (t - a.t) / (float)(b.t - a.t);
}

// ===============================
// Serializacja slotu
// ===============================

static uint16_t slotAddr(uint8_t slot) {
    return FRAM_ADDR_PROGRAMS + slot * FRAM_PROGRAM_SLOT_SIZE;
}

static uint16_t packPayload(const Program& p, uint8_t* buf) {
    uint16_t n = 0;
    for (int c = 0; c < NUM_CHANNELS; c++) {
        buf[n++] = p.ch[c].count;
        memcpy(buf + n, p.ch[c].pts, p.ch[c].count * 4);
        n += p.ch[c].count * 4;
    }
    return n;
}

static bool unpackPayload(const uint8_t* buf, uint16_t len, Program& p) {
    uint16_t n = 0;
    for (int c = 0; c < NUM_CHANNELS; c++) {
        if (n >= len) return false;
        uint8_t cnt = buf[n++];
        if (cnt > MAX_POINTS || n + cnt * 4 > len) return false;
        p.ch[c].count = cnt;
        memcpy(p.ch[c].pts, buf + n, cnt * 4);
        n += cnt * 4;
        if (!validateCurve(p.ch[c])) return false;
    }
    return n == len;
}

static uint32_t headerCrc(const ProgramHeader& h) {
    return crc32Calc((const uint8_t*)&h + 4, sizeof(h) - 8);  // bez magic i header_crc32
}

static bool readHeader(uint8_t slot, ProgramHeader& h) {
    if (!framRead(slotAddr(slot), &h, sizeof(h))) return false;
    return h.magic == PROGRAM_MAGIC && h.format_version == PROGRAM_FORMAT_VERSION &&
           h.payload_len <= PAYLOAD_MAX && h.header_crc32 == headerCrc(h);
}

static bool readSlot(uint8_t slot, Program& p) {
    ProgramHeader h;
    if (!readHeader(slot, h)) return false;
    uint8_t buf[PAYLOAD_MAX];
    if (!framRead(slotAddr(slot) + sizeof(h), buf, h.payload_len)) return false;
    if (crc32Calc(buf, h.payload_len) != h.payload_crc32) {
        LOG_ERROR("Program slot %u: CRC payloadu niezgodny", slot);
        return false;
    }
    memset(&p, 0, sizeof(p));
    if (!unpackPayload(buf, h.payload_len, p)) return false;
    p.id = h.program_id;
    p.parent_id = h.parent_id;
    p.created_ts = h.created_ts;
    p.flags = h.flags;
    memcpy(p.name, h.name, PROGRAM_NAME_LEN);
    p.name[PROGRAM_NAME_LEN - 1] = '\0';
    return true;
}

// Zapis wg FRAM_MAP.md: payload → nagłówek bez magic → odczyt kontrolny → magic (zatwierdzenie)
static bool writeSlot(uint8_t slot, const Program& p) {
    uint8_t buf[PAYLOAD_MAX];
    uint16_t len = packPayload(p, buf);

    ProgramHeader h;
    memset(&h, 0, sizeof(h));
    h.format_version = PROGRAM_FORMAT_VERSION;
    h.flags = p.flags;
    h.payload_len = len;
    h.program_id = p.id;
    h.parent_id = p.parent_id;
    h.created_ts = p.created_ts;
    h.payload_crc32 = crc32Calc(buf, len);
    memcpy(h.name, p.name, PROGRAM_NAME_LEN);
    h.name[PROGRAM_NAME_LEN - 1] = '\0';
    h.header_crc32 = headerCrc(h);

    uint32_t zero = 0;
    if (!framWrite(slotAddr(slot), &zero, 4)) return false;          // slot niezatwierdzony na czas zapisu
    if (!framWrite(slotAddr(slot) + sizeof(h), buf, len)) return false;
    if (!framWrite(slotAddr(slot), &h, sizeof(h))) return false;      // magic = 0 w h

    static Program check;   // static: ~830 B poza stosem taska AsyncTCP (wywołania pod LampLock)
    h.magic = PROGRAM_MAGIC;
    if (!framWrite(slotAddr(slot), &h.magic, 4)) return false;
    if (!readSlot(slot, check)) {
        framWrite(slotAddr(slot), &zero, 4);
        return false;
    }
    return true;
}

// ===============================
// Katalog
// ===============================

static void addToCatalog(uint8_t slot, const ProgramHeader& h) {
    CatalogEntry& e = s_catalog[s_count++];
    e.id = h.program_id;
    e.created_ts = h.created_ts;
    e.slot = slot;
    e.flags = h.flags;
    memcpy(e.name, h.name, PROGRAM_NAME_LEN);
    e.name[PROGRAM_NAME_LEN - 1] = '\0';
}

static void scanSlots() {
    s_count = 0;
    for (uint8_t slot = 0; slot < FRAM_PROGRAM_SLOTS; slot++) {
        ProgramHeader h;
        if (!readHeader(slot, h)) continue;
        if (isTombstoned(h.program_id) || findProgram(h.program_id)) {
            // Skasowany (tombstone) albo duplikat id — zwalniamy slot
            uint32_t zero = 0;
            framWrite(slotAddr(slot), &zero, 4);
            continue;
        }
        addToCatalog(slot, h);
    }
}

static int freeSlot() {
    for (uint8_t slot = 0; slot < FRAM_PROGRAM_SLOTS; slot++) {
        bool used = false;
        for (uint8_t i = 0; i < s_count; i++) if (s_catalog[i].slot == slot) { used = true; break; }
        if (!used) return slot;
    }
    return -1;
}

uint8_t catalogCount() { return s_count; }

const CatalogEntry* catalogEntry(uint8_t i) { return i < s_count ? &s_catalog[i] : nullptr; }

const CatalogEntry* findProgram(uint64_t id) {
    for (uint8_t i = 0; i < s_count; i++) if (s_catalog[i].id == id) return &s_catalog[i];
    return nullptr;
}

// ===============================
// API
// ===============================

void initProgramStore(bool blankFram) {
    LampLock lock;
    if (!isFramInitialized()) {
        buildFactoryProgram(s_active);
        s_count = 0;
        LOG_ERROR("Program store: brak FRAM — program fabryczny tylko w RAM");
        return;
    }

    scanSlots();

    if (blankFram && !findProgram(FACTORY_PROGRAM_ID)) {
        Program f;
        buildFactoryProgram(f);
        f.created_ts = (uint32_t)time(nullptr);
        int slot = freeSlot();
        if (slot >= 0 && writeSlot(slot, f)) {
            ProgramHeader h;
            readHeader(slot, h);
            addToCatalog(slot, h);
            sysState().active_program_id = FACTORY_PROGRAM_ID;
            saveSystemState();
            LOG_INFO("Program fabryczny zainstalowany (slot %d)", slot);
        }
    }

    uint64_t activeId = sysState().active_program_id;
    if (activeId && loadProgram(activeId, s_active)) {
        LOG_INFO("Aktywny program: %s (%u w bibliotece)", s_active.name, s_count);
        return;
    }

    // Brak / uszkodzony aktywny → pierwszy ważny z katalogu, a gdy pusty — fabryczny w RAM
    for (uint8_t i = 0; i < s_count; i++) {
        if (loadProgram(s_catalog[i].id, s_active)) {
            sysState().active_program_id = s_active.id;
            saveSystemState();
            LOG_WARNING("Aktywny program niedostępny — przełączono na: %s", s_active.name);
            return;
        }
    }
    buildFactoryProgram(s_active);
    LOG_WARNING("Biblioteka pusta — program fabryczny tylko w RAM");
}

const Program& activeProgram() { return s_active; }

bool loadProgram(uint64_t id, Program& out) {
    LampLock lock;
    const CatalogEntry* e = findProgram(id);
    if (!e) return false;
    return readSlot(e->slot, out);
}

ProgramError activateProgram(uint64_t id) {
    LampLock lock;
    Program p;
    if (!findProgram(id)) return PROG_ERR_NOT_FOUND;
    if (!loadProgram(id, p)) return PROG_ERR_STORAGE;
    s_active = p;
    sysState().active_program_id = id;
    saveSystemState();
    LOG_INFO("Aktywowano program: %s", s_active.name);
    return PROG_OK;
}

static uint64_t newProgramId() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    uint64_t id;
    do {
        id = ((uint64_t)esp_random() << 32) | esp_random();
        id ^= ((uint64_t)mac[3] << 16) | ((uint64_t)mac[4] << 8) | mac[5];
    } while (id == 0 || id == FACTORY_PROGRAM_ID || findProgram(id) || isTombstoned(id));
    return id;
}

ProgramError saveNewProgram(Program& p, uint64_t& newId) {
    LampLock lock;
    for (int c = 0; c < NUM_CHANNELS; c++) if (!validateCurve(p.ch[c])) return PROG_ERR_INVALID;
    if (!isFramInitialized()) return PROG_ERR_STORAGE;
    int slot = freeSlot();
    if (slot < 0) return PROG_ERR_FULL;

    p.id = newProgramId();
    p.flags = 0;
    p.created_ts = (uint32_t)time(nullptr);
    p.name[PROGRAM_NAME_LEN - 1] = '\0';
    if (!writeSlot(slot, p)) return PROG_ERR_STORAGE;

    ProgramHeader h;
    readHeader(slot, h);
    addToCatalog(slot, h);
    newId = p.id;
    LOG_INFO("Zapisano program: %s (slot %d)", p.name, slot);
    return PROG_OK;
}

ProgramError deleteProgram(uint64_t id) {
    LampLock lock;
    if (id == s_active.id) return PROG_ERR_ACTIVE;
    for (uint8_t i = 0; i < s_count; i++) {
        if (s_catalog[i].id != id) continue;
        uint32_t zero = 0;
        if (!framWrite(slotAddr(s_catalog[i].slot), &zero, 4)) return PROG_ERR_STORAGE;
        addTombstone(id);
        s_catalog[i] = s_catalog[--s_count];
        LOG_INFO("Skasowano program id=%08lX%08lX", (uint32_t)(id >> 32), (uint32_t)id);
        return PROG_OK;
    }
    return PROG_ERR_NOT_FOUND;
}

const char* programErrorStr(ProgramError e) {
    switch (e) {
        case PROG_OK:            return "ok";
        case PROG_ERR_INVALID:   return "invalid curve";
        case PROG_ERR_FULL:      return "library full (24)";
        case PROG_ERR_NOT_FOUND: return "program not found";
        case PROG_ERR_ACTIVE:    return "cannot delete active program";
        case PROG_ERR_STORAGE:   return "FRAM error";
    }
    return "error";
}
