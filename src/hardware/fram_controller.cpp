#include "fram_controller.h"
#include "../core/logging.h"
#include "../hardware/hardware_pins.h"
#include <SPI.h>
#include <Adafruit_FRAM_SPI.h>
#include "../crypto/fram_encryption.h"
#include "../algorithm/algorithm_config.h"

Adafruit_FRAM_SPI fram(FRAM_SPI_CS_PIN, &SPI, 1000000);
bool framInitialized = false;

// MB85RS64V (SPI FRAM) requires WREN before every write — the write-enable
// latch auto-clears after each write cycle, so it must be re-asserted each time.
static bool framWrite(uint32_t addr, const void* data, size_t len) {
    fram.writeEnable(true);
    return fram.write(addr, (const uint8_t*)data, len);
}

bool isFramInitialized() {
    return framInitialized;
}

bool framReconnect() {
    SPI.begin(FRAM_SPI_SCK_PIN, FRAM_SPI_MISO_PIN, FRAM_SPI_MOSI_PIN, FRAM_SPI_CS_PIN);
    if (!fram.begin()) {
        framInitialized = false;
        LOG_ERROR("framReconnect: FRAM not found on SPI");
        return false;
    }
    framInitialized = true;
    LOG_INFO("framReconnect: FRAM re-initialized on SPI bus");
    return true;
}

bool initFRAM() {
    LOG_INFO("");
    LOG_INFO("Initializing FRAM (SPI, MB85RS64V)...");

    SPI.begin(FRAM_SPI_SCK_PIN, FRAM_SPI_MISO_PIN, FRAM_SPI_MOSI_PIN, FRAM_SPI_CS_PIN);

    if (!fram.begin()) {
        LOG_ERROR("");
        LOG_ERROR("FRAM not found on SPI!");
        framInitialized = false;
        return false;
    }

    framInitialized = true;
    LOG_INFO("");
    LOG_INFO("FRAM initialized successfully (MB85RS64V, 8KB)");

    if (!verifyFRAM()) {
        LOG_WARNING("");
        LOG_WARNING("FRAM magic/version mismatch — reinitializing header");

        uint32_t magic = FRAM_MAGIC_NUMBER;
        framWrite(FRAM_ADDR_MAGIC, &magic, 4);

        uint16_t version = FRAM_DATA_VERSION;
        framWrite(FRAM_ADDR_VERSION, &version, 2);

        LOG_INFO("FRAM header written");
    }

    return true;
}

bool verifyFRAM() {
    if (!framInitialized) return false;

    uint32_t magic = 0;
    fram.read(FRAM_ADDR_MAGIC, (uint8_t*)&magic, 4);

    if (magic != FRAM_MAGIC_NUMBER) {
        LOG_WARNING("");
        LOG_WARNING("FRAM magic number mismatch: 0x%08X", magic);
        return false;
    }

    uint16_t version = 0;
    fram.read(FRAM_ADDR_VERSION, (uint8_t*)&version, 2);

    if (version != FRAM_DATA_VERSION) {
        LOG_WARNING("");
        LOG_WARNING("FRAM version mismatch: %d (expected %d)", version, FRAM_DATA_VERSION);
        return false;
    }

    LOG_INFO("");
    LOG_INFO("FRAM verification successful (v%d)", FRAM_DATA_VERSION);
    return true;
}

// ===============================
// FRAM CREDENTIALS SECTION
// (Shared with FRAM Programmer)
// ===============================

bool readCredentialsFromFRAM(FRAMCredentials& creds) {
    if (!framInitialized) {
        LOG_ERROR("");
        LOG_ERROR("FRAM not initialized for credentials read");
        return false;
    }

    fram.read(FRAM_CREDENTIALS_ADDR, (uint8_t*)&creds, sizeof(FRAMCredentials));
    LOG_INFO("");
    LOG_INFO("Read credentials from FRAM at address 0x%04X", FRAM_CREDENTIALS_ADDR);
    return true;
}

bool writeCredentialsToFRAM(const FRAMCredentials& creds) {
    if (!framInitialized) {
        LOG_ERROR("");
        LOG_ERROR("FRAM not initialized for credentials write");
        return false;
    }

    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) {
            LOG_WARNING("FRAM credentials write: retrying after verification mismatch (attempt %d)", attempt + 1);
            if (!framReconnect()) {
                LOG_ERROR("FRAM re-init failed during credentials write recovery");
                return false;
            }
        }

        framWrite(FRAM_CREDENTIALS_ADDR, &creds, sizeof(FRAMCredentials));

        FRAMCredentials verify_creds;
        fram.read(FRAM_CREDENTIALS_ADDR, (uint8_t*)&verify_creds, sizeof(FRAMCredentials));

        if (memcmp(&creds, &verify_creds, sizeof(FRAMCredentials)) == 0) {
            LOG_INFO("");
            LOG_INFO("Credentials written to FRAM at address 0x%04X", FRAM_CREDENTIALS_ADDR);
            return true;
        }

        LOG_WARNING("FRAM write verification mismatch (attempt %d)", attempt + 1);
    }

    LOG_ERROR("");
    LOG_ERROR("FRAM credentials write failed after retry");
    return false;
}

bool verifyCredentialsInFRAM() {
    if (!framInitialized) {
        LOG_ERROR("");
        LOG_ERROR("FRAM not initialized for credentials verify");
        return false;
    }

    FRAMCredentials creds;
    if (!readCredentialsFromFRAM(creds)) {
        return false;
    }

    if (creds.magic != FRAM_MAGIC_NUMBER) {
        LOG_WARNING("");
        LOG_WARNING("Invalid credentials magic number: 0x%08X", creds.magic);
        return false;
    }

    // Accept v1, v2, v3 — historical credential struct versions
    if (creds.version != 0x0001 && creds.version != 0x0002 && creds.version != 0x0003) {
        LOG_WARNING("");
        LOG_WARNING("Invalid credentials version: %d", creds.version);
        return false;
    }

    size_t checksum_offset = offsetof(FRAMCredentials, checksum);
    uint16_t calculated_checksum = calculateChecksum((uint8_t*)&creds, checksum_offset);

    if (creds.checksum != calculated_checksum) {
        LOG_WARNING("");
        LOG_WARNING("Credentials checksum mismatch: stored=%d, calculated=%d",
                    creds.checksum, calculated_checksum);
        return false;
    }
    LOG_INFO("");
    LOG_INFO("Credentials verification successful (version %d)", creds.version);
    return true;
}

// ===============================
// THERMO CONFIG SECTION
// ===============================

bool saveThermoConfigToFRAM(const ThermoConfig& cfg) {
    if (!framInitialized) return false;

    framWrite(FRAM_ADDR_THERMO_CONFIG, &cfg, sizeof(ThermoConfig));

    uint16_t chksum = calculateChecksum((const uint8_t*)&cfg, sizeof(ThermoConfig));
    framWrite(FRAM_ADDR_THERMO_CFG_CHKSUM, &chksum, 2);

    LOG_INFO("ThermoConfig saved: target=%.1f heat_hyst=%.1f cool_start=%.1f cool_full=%.1f",
             cfg.target_temp, cfg.heat_hyst, cfg.cool_start, cfg.cool_full);
    return true;
}

bool loadThermoConfigFromFRAM(ThermoConfig& cfg) {
    if (!framInitialized) return false;

    ThermoConfig tmp;
    fram.read(FRAM_ADDR_THERMO_CONFIG, (uint8_t*)&tmp, sizeof(ThermoConfig));

    uint16_t stored = 0;
    fram.read(FRAM_ADDR_THERMO_CFG_CHKSUM, (uint8_t*)&stored, 2);

    uint16_t calc = calculateChecksum((const uint8_t*)&tmp, sizeof(ThermoConfig));
    if (calc != stored) {
        LOG_WARNING("ThermoConfig checksum mismatch — using defaults");
        return false;
    }

    cfg = tmp;
    return true;
}

// ===============================
// ENERGY STORE SECTION (checksum wbudowany w struct, jak FRAMCredentials)
// ===============================

bool saveEnergyStoreToFRAM(const EnergyStore& store) {
    if (!framInitialized) return false;

    EnergyStore tmp = store;
    size_t checksum_offset = offsetof(EnergyStore, checksum);
    tmp.checksum = calculateChecksum((const uint8_t*)&tmp, checksum_offset);

    framWrite(FRAM_ADDR_ENERGY_STORE, &tmp, sizeof(EnergyStore));

    LOG_INFO("EnergyStore saved: total_pulses=%u day_start=%u ppkwh=%u",
             tmp.total_pulses, tmp.day_start_pulses, tmp.pulses_per_kwh);
    return true;
}

bool loadEnergyStoreFromFRAM(EnergyStore& store) {
    if (!framInitialized) return false;

    EnergyStore tmp;
    fram.read(FRAM_ADDR_ENERGY_STORE, (uint8_t*)&tmp, sizeof(EnergyStore));

    size_t checksum_offset = offsetof(EnergyStore, checksum);
    uint16_t calc = calculateChecksum((const uint8_t*)&tmp, checksum_offset);
    if (calc != tmp.checksum) {
        LOG_WARNING("EnergyStore checksum mismatch — using defaults (0)");
        return false;
    }

    store = tmp;
    return true;
}

bool saveEnergyResetInfoToFRAM(const EnergyResetInfo& info) {
    if (!framInitialized) return false;

    EnergyResetInfo tmp = info;
    size_t checksum_offset = offsetof(EnergyResetInfo, checksum);
    tmp.checksum = calculateChecksum((const uint8_t*)&tmp, checksum_offset);

    framWrite(FRAM_ADDR_ENERGY_RESET, &tmp, sizeof(EnergyResetInfo));

    LOG_INFO("EnergyResetInfo saved: reset_ts=%u", tmp.reset_ts);
    return true;
}

bool loadEnergyResetInfoFromFRAM(EnergyResetInfo& info) {
    if (!framInitialized) return false;

    EnergyResetInfo tmp;
    fram.read(FRAM_ADDR_ENERGY_RESET, (uint8_t*)&tmp, sizeof(EnergyResetInfo));

    size_t checksum_offset = offsetof(EnergyResetInfo, checksum);
    uint16_t calc = calculateChecksum((const uint8_t*)&tmp, checksum_offset);
    if (calc != tmp.checksum) {
        LOG_WARNING("EnergyResetInfo checksum mismatch — using defaults (0)");
        return false;
    }

    info = tmp;
    return true;
}

// ===============================
// Lock-PIN edycji GUI (checksum wbudowany w struct, jak EnergyStore)
// ===============================

bool saveLockPinToFRAM(const LockPin& pin) {
    if (!framInitialized) return false;

    LockPin tmp = pin;
    tmp.pin[sizeof(tmp.pin) - 1] = '\0';
    tmp.magic = LOCK_PIN_MAGIC;
    size_t checksum_offset = offsetof(LockPin, checksum);
    tmp.checksum = calculateChecksum((const uint8_t*)&tmp, checksum_offset);

    framWrite(FRAM_ADDR_LOCK_PIN, &tmp, sizeof(LockPin));
    return true;
}

bool loadLockPinFromFRAM(LockPin& pin) {
    if (!framInitialized) return false;

    LockPin tmp;
    fram.read(FRAM_ADDR_LOCK_PIN, (uint8_t*)&tmp, sizeof(LockPin));

    size_t checksum_offset = offsetof(LockPin, checksum);
    uint16_t calc = calculateChecksum((const uint8_t*)&tmp, checksum_offset);
    if (tmp.magic != LOCK_PIN_MAGIC || calc != tmp.checksum) {
        // Świeży/uszkodzony FRAM — lazy-init do domyślnego PIN-u "1234" (jak dozownik).
        // magic sprawdzany OBOK checksumu — sam checksum (suma bajtów) nie
        // odróżnia "poprawnie zapisanych samych zer" od "nigdy niezapisanej,
        // zerowej pamięci" (patrz komentarz przy LockPin w algorithm_config.h).
        LockPin def{};
        strncpy(def.pin, "1234", sizeof(def.pin) - 1);
        saveLockPinToFRAM(def);
        pin = def;
        return true;
    }

    pin = tmp;
    return true;
}

// ================================================================
// RING BUFFERS — generyczna implementacja (count/wptr/checksum + dane),
// parametryzowana adresami/pojemnością/rozmiarem rekordu. Używana przez
// trzy konkretne bufory poniżej: godzinowy, dobowy, zdarzeń alarmowych.
// ================================================================

static_assert(sizeof(TempAvgRecord) == HOURLY_RECORD_SIZE, "TempAvgRecord/HOURLY_RECORD_SIZE mismatch");
static_assert(sizeof(TempAvgRecord) == DAILY_RECORD_SIZE,  "TempAvgRecord/DAILY_RECORD_SIZE mismatch");
static_assert(sizeof(AlarmEvent)    == ALARM_RECORD_SIZE,  "AlarmEvent/ALARM_RECORD_SIZE mismatch");

// Metadane (count/wptr) chronione osobną checksumą — świeża/pusta FRAM ma
// losowe bajty, bez tego pierwszy odczyt potrafiłby zwrócić śmieciowy count.
static bool readRingMeta(uint16_t countAddr, uint16_t wptrAddr, uint16_t chksumAddr,
                          uint16_t& count, uint16_t& wptr) {
    fram.read(countAddr, (uint8_t*)&count, 2);
    fram.read(wptrAddr,  (uint8_t*)&wptr,  2);

    uint8_t metabuf[4];
    memcpy(metabuf,     &wptr,  2);
    memcpy(metabuf + 2, &count, 2);
    uint16_t stored = 0;
    fram.read(chksumAddr, (uint8_t*)&stored, 2);

    if (calculateChecksum(metabuf, 4) != stored) {
        count = 0;
        wptr  = 0;
        return false;
    }
    return true;
}

static void writeRingMeta(uint16_t countAddr, uint16_t wptrAddr, uint16_t chksumAddr,
                           uint16_t count, uint16_t wptr) {
    framWrite(wptrAddr,  &wptr,  2);
    framWrite(countAddr, &count, 2);

    uint8_t metabuf[4];
    memcpy(metabuf,     &wptr,  2);
    memcpy(metabuf + 2, &count, 2);
    uint16_t chksum = calculateChecksum(metabuf, 4);
    framWrite(chksumAddr, &chksum, 2);
}

static bool saveRingRecord(uint16_t dataAddr, uint16_t capacity, uint16_t recSize,
                            uint16_t countAddr, uint16_t wptrAddr, uint16_t chksumAddr,
                            const void* rec) {
    if (!framInitialized) return false;

    uint16_t count = 0, wptr = 0;
    readRingMeta(countAddr, wptrAddr, chksumAddr, count, wptr);  // checksum invalid → traktuj jako pusty bufor
    if (wptr >= capacity) wptr = 0;

    uint16_t addr = dataAddr + wptr * recSize;
    framWrite(addr, rec, recSize);

    wptr = (wptr + 1) % capacity;
    if (count < capacity) count++;

    writeRingMeta(countAddr, wptrAddr, chksumAddr, count, wptr);
    return true;
}

// buf musi być tablicą elemSize-bajtowych rekordów; wypełnia newest-first.
static uint16_t loadRingRecords(uint16_t dataAddr, uint16_t capacity, uint16_t recSize,
                                 uint16_t countAddr, uint16_t wptrAddr, uint16_t chksumAddr,
                                 void* buf, uint16_t maxCount, size_t elemSize) {
    if (!framInitialized || !buf) return 0;

    uint16_t count = 0, wptr = 0;
    readRingMeta(countAddr, wptrAddr, chksumAddr, count, wptr);
    if (count > capacity) count = capacity;
    if (count > maxCount) count = maxCount;
    if (count == 0) return 0;

    uint8_t* out = (uint8_t*)buf;
    for (uint16_t i = 0; i < count; i++) {
        uint16_t slot = (wptr + capacity - 1 - i) % capacity;
        uint16_t addr = dataAddr + slot * recSize;
        fram.read(addr, out + (size_t)i * elemSize, recSize);
    }
    return count;
}

static uint16_t getRingCount(uint16_t capacity, uint16_t countAddr, uint16_t wptrAddr, uint16_t chksumAddr) {
    if (!framInitialized) return 0;
    uint16_t count = 0, wptr = 0;
    readRingMeta(countAddr, wptrAddr, chksumAddr, count, wptr);
    return (count > capacity) ? capacity : count;
}

// ---- Bufor godzinowy (rolling 24h, TempAvgRecord) ----

bool saveHourlyRecord(const TempAvgRecord& rec) {
    return saveRingRecord(FRAM_ADDR_HOURLY_BUFFER, HOURLY_BUFFER_CAPACITY, HOURLY_RECORD_SIZE,
                           FRAM_ADDR_HOURLY_COUNT, FRAM_ADDR_HOURLY_WPTR, FRAM_ADDR_HOURLY_META_CHKSUM, &rec);
}

uint16_t loadHourlyHistory(TempAvgRecord* buf, uint16_t maxCount) {
    return loadRingRecords(FRAM_ADDR_HOURLY_BUFFER, HOURLY_BUFFER_CAPACITY, HOURLY_RECORD_SIZE,
                            FRAM_ADDR_HOURLY_COUNT, FRAM_ADDR_HOURLY_WPTR, FRAM_ADDR_HOURLY_META_CHKSUM,
                            buf, maxCount, sizeof(TempAvgRecord));
}

uint16_t getHourlyRecordCount() {
    return getRingCount(HOURLY_BUFFER_CAPACITY, FRAM_ADDR_HOURLY_COUNT, FRAM_ADDR_HOURLY_WPTR, FRAM_ADDR_HOURLY_META_CHKSUM);
}

// ---- Bufor dobowy (~rok, TempAvgRecord) ----

bool saveDailyRecord(const TempAvgRecord& rec) {
    return saveRingRecord(FRAM_ADDR_DAILY_BUFFER, DAILY_BUFFER_CAPACITY, DAILY_RECORD_SIZE,
                           FRAM_ADDR_DAILY_COUNT, FRAM_ADDR_DAILY_WPTR, FRAM_ADDR_DAILY_META_CHKSUM, &rec);
}

uint16_t loadDailyHistory(TempAvgRecord* buf, uint16_t maxCount) {
    return loadRingRecords(FRAM_ADDR_DAILY_BUFFER, DAILY_BUFFER_CAPACITY, DAILY_RECORD_SIZE,
                            FRAM_ADDR_DAILY_COUNT, FRAM_ADDR_DAILY_WPTR, FRAM_ADDR_DAILY_META_CHKSUM,
                            buf, maxCount, sizeof(TempAvgRecord));
}

uint16_t getDailyRecordCount() {
    return getRingCount(DAILY_BUFFER_CAPACITY, FRAM_ADDR_DAILY_COUNT, FRAM_ADDR_DAILY_WPTR, FRAM_ADDR_DAILY_META_CHKSUM);
}

// ---- Bufor zdarzeń alarmowych (AlarmEvent) ----

bool saveAlarmEvent(const AlarmEvent& ev) {
    return saveRingRecord(FRAM_ADDR_ALARM_BUFFER, ALARM_BUFFER_CAPACITY, ALARM_RECORD_SIZE,
                           FRAM_ADDR_ALARM_COUNT, FRAM_ADDR_ALARM_WPTR, FRAM_ADDR_ALARM_META_CHKSUM, &ev);
}

uint16_t loadAlarmEvents(AlarmEvent* buf, uint16_t maxCount) {
    return loadRingRecords(FRAM_ADDR_ALARM_BUFFER, ALARM_BUFFER_CAPACITY, ALARM_RECORD_SIZE,
                            FRAM_ADDR_ALARM_COUNT, FRAM_ADDR_ALARM_WPTR, FRAM_ADDR_ALARM_META_CHKSUM,
                            buf, maxCount, sizeof(AlarmEvent));
}

uint16_t getAlarmEventCount() {
    return getRingCount(ALARM_BUFFER_CAPACITY, FRAM_ADDR_ALARM_COUNT, FRAM_ADDR_ALARM_WPTR, FRAM_ADDR_ALARM_META_CHKSUM);
}

// ---- Czyszczenie (count=0, wptr=0 — dane pod spodem zostają, tylko niewidoczne) ----

void clearHourlyHistory() {
    writeRingMeta(FRAM_ADDR_HOURLY_COUNT, FRAM_ADDR_HOURLY_WPTR, FRAM_ADDR_HOURLY_META_CHKSUM, 0, 0);
}

void clearDailyHistory() {
    writeRingMeta(FRAM_ADDR_DAILY_COUNT, FRAM_ADDR_DAILY_WPTR, FRAM_ADDR_DAILY_META_CHKSUM, 0, 0);
}

void clearAlarmEvents() {
    writeRingMeta(FRAM_ADDR_ALARM_COUNT, FRAM_ADDR_ALARM_WPTR, FRAM_ADDR_ALARM_META_CHKSUM, 0, 0);
}
