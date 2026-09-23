#include "energy_meter.h"
#include "hardware_pins.h"
#include "hardware/fram_controller.h"
#include "hardware/rtc_controller.h"
#include "core/logging.h"

static volatile uint32_t s_pulse_count    = 0;
static uint32_t          s_day_start      = 0;
static uint16_t          s_pulses_per_kwh = 1000;
static uint32_t          s_last_pulse_ms  = 0;
static uint32_t          s_prev_pulse_ms  = 0;
static uint32_t          s_last_saved_count = 0;  // do progu zapisu co N impulsów
static uint32_t          s_reset_ts       = 0;    // Unix UTC ostatniego kasowania licznika total

static const uint32_t ENERGY_SAVE_EVERY_N_PULSES = 10;

// Min. odstęp między akceptowanymi impulsami — odrzuca zbocza fizycznie
// niemożliwe dla licznika S0 (drgania styków / szum EMI z sąsiadującego kabla
// 230V, patrz pamięć projektu). 250ms = maks. 4 imp/s = ok. 14,4 kW przy
// pulses_per_kwh=1000 — wciąż duży zapas ponad realną moc grzałki/pompy.
// Podniesione z 50ms 2026-07-25: podwójne zliczanie pojedynczego impulsu
// (wolne, zaszumione zbocze narastające po impulsie) dawało pary odległe
// o ok. 100ms, więc 50ms filtra tego nie łapało — patrz log debug w
// updateEnergyMeter().
static const uint32_t MIN_PULSE_INTERVAL_MS = 250;

void IRAM_ATTR onEnergyPulse() {
    uint32_t now = millis();
    if (s_last_pulse_ms != 0 && (now - s_last_pulse_ms) < MIN_PULSE_INTERVAL_MS) {
        return;  // zbyt szybko od poprzedniego — szum, nie realny impuls
    }
    s_prev_pulse_ms = s_last_pulse_ms;
    s_last_pulse_ms = now;
    s_pulse_count++;
}

void initEnergyMeter(uint16_t pulses_per_kwh) {
    s_pulses_per_kwh = pulses_per_kwh;
    pinMode(ENERGY_PULSE_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ENERGY_PULSE_PIN), onEnergyPulse, FALLING);
    energyLoadFromFRAM();
    LOG_INFO("EnergyMeter: init, ppkwh=%d", s_pulses_per_kwh);
}

void pauseEnergyPulseInterrupt() {
    detachInterrupt(digitalPinToInterrupt(ENERGY_PULSE_PIN));
}

void resumeEnergyPulseInterrupt() {
    attachInterrupt(digitalPinToInterrupt(ENERGY_PULSE_PIN), onEnergyPulse, FALLING);
}

void updateEnergyMeter() {
    uint32_t count;
    noInterrupts(); count = s_pulse_count; interrupts();

    if (count - s_last_saved_count >= ENERGY_SAVE_EVERY_N_PULSES) {
        energySaveToFRAM();
    }
}

void  setEnergyPulsesPerKwh(uint16_t ppk) { s_pulses_per_kwh = ppk; }

float getEnergyKwhTotal() {
    uint32_t count;
    noInterrupts(); count = s_pulse_count; interrupts();
    return (float)count / s_pulses_per_kwh;
}

float getEnergyKwhToday() {
    uint32_t count;
    noInterrupts(); count = s_pulse_count; interrupts();
    return (float)(count - s_day_start) / s_pulses_per_kwh;
}

uint32_t getEnergyPulseCount() {
    uint32_t count;
    noInterrupts(); count = s_pulse_count; interrupts();
    return count;
}

float getPowerWatt() {
    // moc z czasu między ostatnimi dwoma impulsami
    if (s_prev_pulse_ms == 0 || s_last_pulse_ms == s_prev_pulse_ms) return 0.0f;
    uint32_t dt_ms = s_last_pulse_ms - s_prev_pulse_ms;
    if (dt_ms == 0) return 0.0f;
    // 1 imp = 1 Wh (przy 1000 imp/kWh) → moc = 3600000 / (dt_ms * ppkwh / 1000)
    return 3600000.0f / ((float)dt_ms * s_pulses_per_kwh / 1000.0f);
}

void energyResetDayCounter() {
    noInterrupts(); s_day_start = s_pulse_count; interrupts();
    energySaveToFRAM();
}

uint32_t getEnergyResetTimestamp() { return s_reset_ts; }

void energyResetTotalCounter() {
    noInterrupts(); s_pulse_count = 0; interrupts();
    s_day_start = 0;
    s_reset_ts  = (uint32_t)getUnixTimestamp();
    energySaveToFRAM();

    EnergyResetInfo rinfo;
    rinfo.reset_ts   = s_reset_ts;
    rinfo._reserved  = 0;
    rinfo.checksum   = 0;  // przeliczane wewnątrz saveEnergyResetInfoToFRAM()
    saveEnergyResetInfoToFRAM(rinfo);

    LOG_INFO("EnergyMeter: total counter reset, reset_ts=%u", s_reset_ts);
}

void energyLoadFromFRAM() {
    EnergyStore store;
    if (!loadEnergyStoreFromFRAM(store)) {
        LOG_WARNING("EnergyStore not found/invalid in FRAM — starting from 0");
        s_last_saved_count = 0;
    } else {
        noInterrupts();
        s_pulse_count = store.total_pulses;
        interrupts();
        s_day_start = store.day_start_pulses;
        if (store.pulses_per_kwh > 0) s_pulses_per_kwh = store.pulses_per_kwh;
        s_last_saved_count = store.total_pulses;

        LOG_INFO("EnergyStore loaded from FRAM: total_pulses=%u day_start=%u ppkwh=%u",
                 store.total_pulses, store.day_start_pulses, s_pulses_per_kwh);
    }

    EnergyResetInfo rinfo;
    if (loadEnergyResetInfoFromFRAM(rinfo)) {
        s_reset_ts = rinfo.reset_ts;
    }
}

void energySaveToFRAM() {
    uint32_t count;
    noInterrupts(); count = s_pulse_count; interrupts();

    EnergyStore store;
    store.total_pulses     = count;
    store.day_start_pulses = s_day_start;
    store.pulses_per_kwh   = s_pulses_per_kwh;
    store.checksum         = 0;  // przeliczane wewnątrz saveEnergyStoreToFRAM()

    if (saveEnergyStoreToFRAM(store)) {
        s_last_saved_count = count;
    }
}
