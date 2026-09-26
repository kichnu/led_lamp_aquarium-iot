#include "lamp_storage.h"
#include "../hardware/fram_controller.h"
#include "../core/logging.h"
#include <time.h>

static_assert(sizeof(FramHeader)    == 32,  "FramHeader size");
static_assert(sizeof(SystemState)   == 64,  "SystemState size");
static_assert(sizeof(LampConfig)    == 128, "LampConfig size");
static_assert(sizeof(ChannelConfig) == 32,  "ChannelConfig size");
static_assert(sizeof(TombstoneMeta) == 16,  "TombstoneMeta size");
static_assert(FRAM_ADDR_SYSTEM_STATE >= FRAM_CREDENTIALS_ADDR + FRAM_CREDENTIALS_SIZE, "SYSTEM_STATE overlaps CREDENTIALS");
static_assert(FRAM_ADDR_LAMP_CONFIG  >= FRAM_ADDR_SYSTEM_STATE + sizeof(SystemState), "LAMP_CONFIG overlap");
static_assert(FRAM_ADDR_CHANNEL_CFG  >= FRAM_ADDR_LAMP_CONFIG + sizeof(LampConfig), "CHANNEL_CFG overlap");
static_assert(FRAM_ADDR_LOCK_PIN     >= FRAM_ADDR_CHANNEL_CFG + NUM_CHANNELS * sizeof(ChannelConfig), "LOCK_PIN overlap");
static_assert(FRAM_ADDR_TOMBSTONES + sizeof(TombstoneMeta) + FRAM_TOMBSTONE_CAPACITY * 8 <= 0x0C00, "TOMBSTONES overflow");

const char CHANNEL_NAMES[NUM_CHANNELS] = { 'A', 'B', 'C', 'D' };

static SystemState   s_state;
static LampConfig    s_lamp;
static ChannelConfig s_ch[NUM_CHANNELS];
static TombstoneMeta s_tombMeta;
static uint64_t      s_tombIds[FRAM_TOMBSTONE_CAPACITY];

// ===============================
// Sekcja = struct z magic na początku i crc32 na końcu (CRC wszystkiego przed crc32)
// ===============================

template <typename T>
static bool loadSection(uint16_t addr, T& out, uint32_t magic) {
    T tmp;
    if (!framRead(addr, &tmp, sizeof(T))) return false;
    uint32_t stored;
    memcpy(&stored, (uint8_t*)&tmp + sizeof(T) - 4, 4);
    uint32_t firstWord;
    memcpy(&firstWord, &tmp, 4);
    if (firstWord != magic || stored != crc32Calc(&tmp, sizeof(T) - 4)) return false;
    out = tmp;
    return true;
}

template <typename T>
static bool saveSection(uint16_t addr, T& data, uint32_t magic) {
    memcpy(&data, &magic, 4);
    uint32_t crc = crc32Calc(&data, sizeof(T) - 4);
    memcpy((uint8_t*)&data + sizeof(T) - 4, &crc, 4);
    return framWrite(addr, &data, sizeof(T));
}

// ===============================
// Wartości domyślne
// ===============================

void defaultLampConfig(LampConfig& c) {
    memset(&c, 0, sizeof(c));
    c.ramp_s      = RAMP_S_DEFAULT;
    c.fan_on_pct  = FAN_ON_PCT_DEFAULT;
    c.fan_off_pct = FAN_OFF_PCT_DEFAULT;
    for (int i = 0; i < NUM_CHANNELS; i++) c.night_preset[i] = 50;  // 0,5 %
}

void defaultChannelConfig(uint8_t ch, ChannelConfig& c) {
    static const uint16_t POWER_FRAC[NUM_CHANNELS] = { 1500, 1000, 6000, 1500 };  // ‱, USTALENIA.md
    memset(&c, 0, sizeof(c));
    c.gamma      = 1.0f;
    c.min_duty   = 0;
    c.power_frac = POWER_FRAC[ch];
    c.label[0]   = CHANNEL_NAMES[ch];
}

static void defaultSystemState(SystemState& s) {
    memset(&s, 0, sizeof(s));
}

// ===============================
// Init
// ===============================

static void loadOrDefaultAll(bool forceDefaults) {
    if (forceDefaults || !loadSection(FRAM_ADDR_SYSTEM_STATE, s_state, SYSTEM_STATE_MAGIC)) {
        if (!forceDefaults) LOG_WARNING("SYSTEM_STATE: brak/uszkodzony — domyślne");
        defaultSystemState(s_state);
        saveSystemState();
    }
    if (forceDefaults || !loadSection(FRAM_ADDR_LAMP_CONFIG, s_lamp, LAMP_CONFIG_MAGIC)) {
        if (!forceDefaults) LOG_WARNING("LAMP_CONFIG: brak/uszkodzony — domyślne");
        defaultLampConfig(s_lamp);
        saveLampConfig();
    }
    for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
        uint16_t addr = FRAM_ADDR_CHANNEL_CFG + i * sizeof(ChannelConfig);
        if (forceDefaults || !loadSection(addr, s_ch[i], CHANNEL_CONFIG_MAGIC)) {
            if (!forceDefaults) LOG_WARNING("CHANNEL_CONFIG %c: brak/uszkodzony — domyślne", CHANNEL_NAMES[i]);
            defaultChannelConfig(i, s_ch[i]);
            saveChannelConfig(i);
        }
    }
    if (forceDefaults || !loadSection(FRAM_ADDR_TOMBSTONES, s_tombMeta, TOMBSTONE_MAGIC)
        || s_tombMeta.count > FRAM_TOMBSTONE_CAPACITY || s_tombMeta.wptr >= FRAM_TOMBSTONE_CAPACITY) {
        if (!forceDefaults) LOG_WARNING("TOMBSTONES: brak/uszkodzone — pusta lista");
        memset(&s_tombMeta, 0, sizeof(s_tombMeta));
        saveSection(FRAM_ADDR_TOMBSTONES, s_tombMeta, TOMBSTONE_MAGIC);
        memset(s_tombIds, 0, sizeof(s_tombIds));
    } else {
        framRead(FRAM_ADDR_TOMBSTONES + sizeof(TombstoneMeta), s_tombIds, s_tombMeta.count * 8);
    }
}

bool initLampStorage(bool& blankFram) {
    blankFram = false;

    if (!isFramInitialized()) {
        LOG_ERROR("Lamp storage: brak FRAM — konfiguracja domyślna tylko w RAM");
        defaultSystemState(s_state);
        defaultLampConfig(s_lamp);
        for (uint8_t i = 0; i < NUM_CHANNELS; i++) defaultChannelConfig(i, s_ch[i]);
        memset(&s_tombMeta, 0, sizeof(s_tombMeta));
        return false;
    }

    FramHeader h;
    bool readOk = framRead(FRAM_ADDR_HEADER, &h, sizeof(h));
    bool magicOk = readOk && h.magic == FRAM_MAGIC_NUMBER;
    bool crcOk = magicOk && h.crc32 == crc32Calc(&h, sizeof(h) - 4);
    bool versionOk = crcOk && h.layout_version == FRAM_LAYOUT_VERSION;

    if (!versionOk) {
        if (!magicOk || !crcOk) {
            LOG_WARNING("FRAM: brak nagłówka RL90 — inicjalizacja pustej pamięci");
            blankFram = true;
        } else {
            LOG_WARNING("FRAM: layout v%u → v%u — reinicjalizacja obszaru systemowego (bez poświadczeń i programów)",
                        h.layout_version, FRAM_LAYOUT_VERSION);
        }
        memset(&h, 0, sizeof(h));
        h.magic = FRAM_MAGIC_NUMBER;
        h.layout_version = FRAM_LAYOUT_VERSION;
        h.init_ts = (uint32_t)time(nullptr);
        h.crc32 = crc32Calc(&h, sizeof(h) - 4);
        framWrite(FRAM_ADDR_HEADER, &h, sizeof(h));
        loadOrDefaultAll(true);
    } else {
        loadOrDefaultAll(false);
    }

    LOG_INFO("Lamp storage OK: ramp=%us fan=%u/%u%% tombstones=%u",
             s_lamp.ramp_s, s_lamp.fan_on_pct, s_lamp.fan_off_pct, s_tombMeta.count);
    return true;
}

// ===============================
// Dostęp / zapis
// ===============================

SystemState&   sysState()                  { return s_state; }
LampConfig&    lampConfig()                { return s_lamp; }
ChannelConfig& channelConfig(uint8_t ch)   { return s_ch[ch < NUM_CHANNELS ? ch : 0]; }

bool saveSystemState() {
    return isFramInitialized() && saveSection(FRAM_ADDR_SYSTEM_STATE, s_state, SYSTEM_STATE_MAGIC);
}

bool saveLampConfig() {
    return isFramInitialized() && saveSection(FRAM_ADDR_LAMP_CONFIG, s_lamp, LAMP_CONFIG_MAGIC);
}

bool saveChannelConfig(uint8_t ch) {
    if (ch >= NUM_CHANNELS || !isFramInitialized()) return false;
    return saveSection(FRAM_ADDR_CHANNEL_CFG + ch * sizeof(ChannelConfig), s_ch[ch], CHANNEL_CONFIG_MAGIC);
}

// ===============================
// Tombstone — ring 126 id, po zapełnieniu nadpisywany najstarszy
// ===============================

bool isTombstoned(uint64_t id) {
    for (uint16_t i = 0; i < s_tombMeta.count; i++) if (s_tombIds[i] == id) return true;
    return false;
}

bool addTombstone(uint64_t id) {
    if (id == FACTORY_PROGRAM_ID) return false;
    if (isTombstoned(id)) return true;
    uint16_t slot = s_tombMeta.wptr;
    s_tombIds[slot] = id;
    s_tombMeta.wptr = (slot + 1) % FRAM_TOMBSTONE_CAPACITY;
    if (s_tombMeta.count < FRAM_TOMBSTONE_CAPACITY) s_tombMeta.count++;
    if (!isFramInitialized()) return true;
    // Najpierw id, potem meta — zanik zasilania między zapisami gubi najwyżej ten wpis
    bool ok = framWrite(FRAM_ADDR_TOMBSTONES + sizeof(TombstoneMeta) + slot * 8, &id, 8);
    return saveSection(FRAM_ADDR_TOMBSTONES, s_tombMeta, TOMBSTONE_MAGIC) && ok;
}

bool removeTombstone(uint64_t id) {
    if (!isTombstoned(id)) return true;
    // Ring → tablica liniowa od najstarszego, bez usuwanego id; wptr = count
    uint64_t tmp[FRAM_TOMBSTONE_CAPACITY];
    uint16_t n = 0;
    uint16_t start = (s_tombMeta.count < FRAM_TOMBSTONE_CAPACITY) ? 0 : s_tombMeta.wptr;
    for (uint16_t i = 0; i < s_tombMeta.count; i++) {
        uint64_t v = s_tombIds[(start + i) % FRAM_TOMBSTONE_CAPACITY];
        if (v != id) tmp[n++] = v;
    }
    memcpy(s_tombIds, tmp, n * 8);
    s_tombMeta.count = n;
    s_tombMeta.wptr = n % FRAM_TOMBSTONE_CAPACITY;
    if (!isFramInitialized()) return true;
    // Najpierw ids, potem meta — zanik zasilania między zapisami zostawia starą (spójną) meta
    bool ok = (n == 0) || framWrite(FRAM_ADDR_TOMBSTONES + sizeof(TombstoneMeta), s_tombIds, n * 8);
    return saveSection(FRAM_ADDR_TOMBSTONES, s_tombMeta, TOMBSTONE_MAGIC) && ok;
}
