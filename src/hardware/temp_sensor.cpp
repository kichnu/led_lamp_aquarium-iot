#include "temp_sensor.h"
#include "hardware_pins.h"
#include "algorithm_config.h"
#include "core/logging.h"
#include <Wire.h>

// Sensirion STS35 single-shot high repeatability
// Komenda: 0x24 0x00 → czekaj 15ms → czytaj 3 bajty (temp_H, temp_L, CRC8)
// CRC8: poly 0x31, init 0xFF

// TODO: Etap 1d — pełna implementacja async state machine

static float    s_last_temp   = 0.0f;
static uint8_t  s_fault_count = 0;
static bool     s_valid       = false;

static uint8_t sts35_crc(uint8_t msb, uint8_t lsb) {
    uint8_t crc = 0xFF;
    uint8_t data[2] = {msb, lsb};
    for (int i = 0; i < 2; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
        }
    }
    return crc;
}

static bool sts35_read(float& out_temp) {
    // Trigger single-shot high repeatability
    Wire1.beginTransmission(STS35_I2C_ADDR);
    Wire1.write(0x24);
    Wire1.write(0x00);
    if (Wire1.endTransmission() != 0) return false;

    delay(16);  // 15ms conversion + margin

    if (Wire1.requestFrom((uint8_t)STS35_I2C_ADDR, (uint8_t)3) != 3) return false;
    uint8_t msb = Wire1.read();
    uint8_t lsb = Wire1.read();
    uint8_t crc = Wire1.read();

    if (sts35_crc(msb, lsb) != crc) return false;

    uint16_t raw = ((uint16_t)msb << 8) | lsb;
    out_temp = 175.0f * raw / 65535.0f - 45.0f;
    return true;
}

void initTempSensor() {
    Wire1.beginTransmission(STS35_I2C_ADDR);
    uint8_t err = Wire1.endTransmission();
    if (err == 0) {
        LOG_INFO("TempSensor: STS35 found at 0x%02X", STS35_I2C_ADDR);
        s_fault_count = 0;
    } else {
        LOG_ERROR("TempSensor: STS35 NOT found! (I2C err=%d)", err);
        s_fault_count = SENSOR_FAULT_THRESHOLD;
    }
}

// Warstwa 1 tolerancji na przejściowe zakłócenia I2C (SSR/PWM w tej samej
// obudowie = realne źródło EMI nawet przy sprawnym kablu): do STS35_RETRY_COUNT
// prób w jednym cyklu, zanim w ogóle policzymy cykl jako nieudany. Warstwa 2
// (SENSOR_FAULT_THRESHOLD, kolejne w PEŁNI nieudane cykle) zostaje bez zmian
// jako backstop na realne, trwałe usterki.
void updateTempSensor() {
    float temp;
    bool ok = false;
    for (uint8_t attempt = 0; attempt < STS35_RETRY_COUNT && !ok; attempt++) {
        if (attempt > 0) delay(STS35_RETRY_DELAY_MS);
        ok = sts35_read(temp);
    }

    if (ok) {
        s_last_temp   = temp;
        s_valid       = true;
        s_fault_count = 0;
    } else {
        s_fault_count++;
        if (s_fault_count >= SENSOR_FAULT_THRESHOLD) {
            s_valid = false;
            LOG_ERROR("TempSensor: fault count=%d (cykl nieudany po %d próbach)", s_fault_count, STS35_RETRY_COUNT);
        }
    }
}

bool    isTempValid()        { return s_valid; }
float   getTemperature()     { return s_last_temp; }
int16_t getTemperatureX10()  { return (int16_t)(s_last_temp * 10.0f); }
uint8_t getSensorFaultCount(){ return s_fault_count; }
