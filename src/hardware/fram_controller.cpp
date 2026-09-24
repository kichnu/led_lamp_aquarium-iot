#include "fram_controller.h"
#include "../core/logging.h"
#include "../hardware/hardware_pins.h"
#include "../crypto/fram_encryption.h"
#include "../core/lamp_lock.h"
#include <Wire.h>

#define FRAM_CHUNK       64      // bajtów danych na transakcję (bufor Wire 128 B)
#define I2C_TIMEOUT_MS   50      // dużo poniżej 5 s TWDT

static bool framInitialized = false;

static_assert(sizeof(FRAMCredentials) == FRAM_CREDENTIALS_SIZE, "FRAMCredentials size mismatch");

// ===============================
// MAGISTRALA I2C
// ===============================

void initI2CBus() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_CLOCK_HZ);
    Wire.setTimeOut(I2C_TIMEOUT_MS);
}

// Slave zawieszony w połowie bajtu (reset MCU w trakcie transakcji) trzyma SDA
// w LOW — 9 taktów SCL pozwala mu dokończyć bajt, potem warunek STOP.
bool i2cBusRecover() {
    LOG_WARNING("I2C: odzyskiwanie magistrali");
    Wire.end();
    pinMode(I2C_SDA_PIN, INPUT_PULLUP);
    pinMode(I2C_SCL_PIN, OUTPUT_OPEN_DRAIN);
    for (int i = 0; i < 9 && digitalRead(I2C_SDA_PIN) == LOW; i++) {
        digitalWrite(I2C_SCL_PIN, LOW);  delayMicroseconds(5);
        digitalWrite(I2C_SCL_PIN, HIGH); delayMicroseconds(5);
    }
    pinMode(I2C_SDA_PIN, OUTPUT_OPEN_DRAIN);
    digitalWrite(I2C_SDA_PIN, LOW);  delayMicroseconds(5);
    digitalWrite(I2C_SCL_PIN, HIGH); delayMicroseconds(5);
    digitalWrite(I2C_SDA_PIN, HIGH); delayMicroseconds(5);
    bool sdaFree = digitalRead(I2C_SDA_PIN) == HIGH;
    initI2CBus();
    return sdaFree;
}

// ===============================
// CRC32 (IEEE 802.3, bitowo — dane są małe, bez tablicy w RAM)
// ===============================

uint32_t crc32Calc(const void* data, size_t len, uint32_t crc) {
    const uint8_t* p = (const uint8_t*)data;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// ===============================
// ODCZYT / ZAPIS BLOKOWY
// ===============================

static bool readChunk(uint16_t addr, uint8_t* buf, size_t len) {
    Wire.beginTransmission(FRAM_I2C_ADDR);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)FRAM_I2C_ADDR, (uint8_t)len) != len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

static bool writeChunk(uint16_t addr, const uint8_t* buf, size_t len) {
    Wire.beginTransmission(FRAM_I2C_ADDR);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    Wire.write(buf, len);
    return Wire.endTransmission() == 0;
}

bool framRead(uint16_t addr, void* buf, size_t len) {
    if (!framInitialized || (uint32_t)addr + len > FRAM_SIZE) return false;
    LampLock lock;
    uint8_t* p = (uint8_t*)buf;
    while (len) {
        size_t n = len > FRAM_CHUNK ? FRAM_CHUNK : len;
        if (!readChunk(addr, p, n)) {
            i2cBusRecover();
            if (!readChunk(addr, p, n)) {
                LOG_ERROR("FRAM read failed @0x%04X", addr);
                return false;
            }
        }
        addr += n; p += n; len -= n;
    }
    return true;
}

bool framWrite(uint16_t addr, const void* buf, size_t len) {
    if (!framInitialized || (uint32_t)addr + len > FRAM_SIZE) return false;
    LampLock lock;
    const uint8_t* p = (const uint8_t*)buf;
    uint8_t verify[FRAM_CHUNK];
    while (len) {
        size_t n = len > FRAM_CHUNK ? FRAM_CHUNK : len;
        bool ok = false;
        for (int attempt = 0; attempt < 2 && !ok; attempt++) {
            if (attempt > 0) i2cBusRecover();
            ok = writeChunk(addr, p, n) && readChunk(addr, verify, n) && memcmp(p, verify, n) == 0;
        }
        if (!ok) {
            LOG_ERROR("FRAM write failed @0x%04X", addr);
            return false;
        }
        addr += n; p += n; len -= n;
    }
    return true;
}

// ===============================
// INIT
// ===============================

bool isFramInitialized() {
    return framInitialized;
}

bool initFRAM() {
    LOG_INFO("Initializing FRAM (I2C 0x%02X, 32 KB)...", FRAM_I2C_ADDR);
    Wire.beginTransmission(FRAM_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        i2cBusRecover();
        Wire.beginTransmission(FRAM_I2C_ADDR);
        if (Wire.endTransmission() != 0) {
            LOG_ERROR("FRAM not found at 0x%02X!", FRAM_I2C_ADDR);
            framInitialized = false;
            return false;
        }
    }
    framInitialized = true;
    LOG_INFO("FRAM OK");
    return true;
}

// ===============================
// CREDENTIALS
// ===============================

bool readCredentialsFromFRAM(FRAMCredentials& creds) {
    if (!framRead(FRAM_CREDENTIALS_ADDR, &creds, sizeof(FRAMCredentials))) {
        LOG_ERROR("FRAM credentials read failed");
        return false;
    }
    return true;
}

bool writeCredentialsToFRAM(const FRAMCredentials& creds) {
    if (!framWrite(FRAM_CREDENTIALS_ADDR, &creds, sizeof(FRAMCredentials))) {
        LOG_ERROR("FRAM credentials write failed");
        return false;
    }
    LOG_INFO("Credentials written to FRAM at 0x%04X", FRAM_CREDENTIALS_ADDR);
    return true;
}

bool verifyCredentialsInFRAM() {
    FRAMCredentials creds;
    if (!readCredentialsFromFRAM(creds)) return false;

    if (creds.magic != FRAM_MAGIC_NUMBER) {
        LOG_WARNING("Invalid credentials magic: 0x%08X", creds.magic);
        return false;
    }
    if (creds.version != 0x0001 && creds.version != 0x0002 && creds.version != 0x0003) {
        LOG_WARNING("Invalid credentials version: %d", creds.version);
        return false;
    }
    size_t checksum_offset = offsetof(FRAMCredentials, checksum);
    uint16_t calculated = calculateChecksum((uint8_t*)&creds, checksum_offset);
    if (creds.checksum != calculated) {
        LOG_WARNING("Credentials checksum mismatch: stored=%d, calculated=%d", creds.checksum, calculated);
        return false;
    }
    LOG_INFO("Credentials verification successful (version %d)", creds.version);
    return true;
}
