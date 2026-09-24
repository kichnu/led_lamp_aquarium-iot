#include "light_engine.h"
#include "lamp_storage.h"
#include "program_store.h"
#include "../hardware/pwm_output.h"
#include "../hardware/rtc_controller.h"
#include "../core/logging.h"
#include "../core/lamp_lock.h"
#include <time.h>
#include <math.h>

#define RESTART_WINDOW_END_MIN  60              // okno 00:00–01:00 lokalnie
#define RESTART_MIN_UPTIME_MS   (30UL * 60UL * 1000UL)
#define TEST_START_V            5000            // 50 %

static LampMode s_mode = MODE_PROGRAM;
static uint16_t s_manual[NUM_CHANNELS] = { 0 };

static float    s_out[NUM_CHANNELS]      = { 0 };   // ostatnio wysłane v
static float    s_target[NUM_CHANNELS]   = { 0 };
static float    s_rampFrom[NUM_CHANNELS] = { 0 };
static bool     s_rampActive = false;
static uint32_t s_rampStartMs = 0;
static bool     s_timeWasValid = false;

static float    s_powerPct = 0;
static bool     s_fanOn = false;
static uint8_t  s_fanPct = 0;
static uint32_t s_lastFanMs = 0;
static bool     s_fanEvaluated = false;
static uint16_t s_minuteOfDay = 0;

// ===============================
// Pomocnicze
// ===============================

uint32_t vToDuty(uint8_t ch, float v) {
    if (v <= 0) return 0;
    if (v > V_MAX) v = V_MAX;
    const ChannelConfig& cc = channelConfig(ch);
    float g = (cc.gamma > 0.1f && cc.gamma < 5.0f) ? cc.gamma : 1.0f;
    uint32_t duty = (uint32_t)lroundf(powf(v / (float)V_MAX, g) * PWM_MAX_DUTY);
    if (duty < cc.min_duty) duty = cc.min_duty;
    return duty > PWM_MAX_DUTY ? PWM_MAX_DUTY : duty;
}

static bool localTime(struct tm& t) {
    if (!isTimeValid()) return false;
    time_t now = time(nullptr);
    localtime_r(&now, &t);
    return true;
}

// Cel wg trybu: program z krzywej (bez czasu = 0) albo suwaki
static void computeTargets() {
    if (s_mode != MODE_PROGRAM) {
        for (int c = 0; c < NUM_CHANNELS; c++) s_target[c] = s_manual[c];
        return;
    }
    struct tm t;
    if (!localTime(t)) {
        for (int c = 0; c < NUM_CHANNELS; c++) s_target[c] = 0;
        return;
    }
    float minute = t.tm_hour * 60 + t.tm_min + t.tm_sec / 60.0f;
    s_minuteOfDay = (uint16_t)minute;
    const Program& p = activeProgram();
    for (int c = 0; c < NUM_CHANNELS; c++) s_target[c] = evalCurve(p.ch[c], minute);
}

static void startRamp() {
    for (int c = 0; c < NUM_CHANNELS; c++) s_rampFrom[c] = s_out[c];
    s_rampStartMs = millis();
    s_rampActive = true;
}

static void applyOutputs() {
    for (int c = 0; c < NUM_CHANNELS; c++) setLedDuty(c, vToDuty(c, s_out[c]));
}

// P = Σ power_frac · duty — ta sama wartość w GUI i dla wentylatora
static void computePower() {
    float p = 0;
    for (int c = 0; c < NUM_CHANNELS; c++)
        p += channelConfig(c).power_frac / 100.0f * getLedDuty(c) / (float)PWM_MAX_DUTY;
    s_powerPct = p;
}

static void updateFan(bool force) {
    uint32_t now = millis();
    if (!force && s_fanEvaluated && now - s_lastFanMs < FAN_UPDATE_MS) return;
    s_lastFanMs = now;
    s_fanEvaluated = true;

    const LampConfig& lc = lampConfig();
    bool on = s_fanOn ? (s_powerPct >= lc.fan_off_pct) : (s_powerPct >= lc.fan_on_pct);
    uint8_t pct = on ? (uint8_t)(s_powerPct > 100 ? 100 : lroundf(s_powerPct)) : 0;
    if (on != s_fanOn) LOG_INFO("Wentylator %s (P=%.1f%%)", on ? "ON" : "OFF", s_powerPct);
    s_fanOn = on;
    s_fanPct = pct;
    setFanDuty((uint32_t)pct * PWM_MAX_DUTY / 100);
}

// ===============================
// Restart dobowy (§15 handoffu)
// ===============================

static void checkDailyRestart() {
    if (s_mode != MODE_PROGRAM || s_rampActive || millis() < RESTART_MIN_UPTIME_MS) return;
    struct tm t;
    if (!localTime(t)) return;
    if (t.tm_hour * 60 + t.tm_min >= RESTART_WINDOW_END_MIN) return;
    for (int c = 0; c < NUM_CHANNELS; c++) if (s_target[c] > 0 || s_out[c] > 0) return;

    uint32_t day = (uint32_t)(t.tm_year + 1900) * 1000 + t.tm_yday;
    SystemState& ss = sysState();
    if (ss.last_restart_day == day) return;
    ss.last_restart_day = day;
    saveSystemState();   // flaga dnia przed restartem — ochrona przed pętlą restartów
    LOG_INFO("Restart dobowy: heap=%u largest=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    restartWithLedsHeldLow();
}

// ===============================
// API
// ===============================

void initLightEngine(bool bootWithHold) {
    LampLock lock;
    s_mode = MODE_PROGRAM;
    s_timeWasValid = isTimeValid();
    computeTargets();
    if (bootWithHold && s_timeWasValid) {
        // Po restarcie planowym od razu wartość programu, bez rampy
        for (int c = 0; c < NUM_CHANNELS; c++) s_out[c] = s_target[c];
        s_rampActive = false;
        LOG_INFO("Start po restarcie dobowym — wartość programu bez rampy");
    } else {
        // Włączenie zasilania / inny reset: rampa od 0
        for (int c = 0; c < NUM_CHANNELS; c++) s_out[c] = 0;
        startRamp();
    }
    applyOutputs();
    computePower();
    updateFan(true);
}

void updateLightEngine() {
    LampLock lock;

    bool tv = isTimeValid();
    if (tv && !s_timeWasValid) {
        LOG_INFO("Czas dostępny — rampa do wartości programu");
        startRamp();
    }
    s_timeWasValid = tv;

    computeTargets();

    if (s_rampActive) {
        uint32_t rampMs = (uint32_t)lampConfig().ramp_s * 1000UL;
        uint32_t el = millis() - s_rampStartMs;
        if (el >= rampMs || rampMs == 0) {
            s_rampActive = false;
            for (int c = 0; c < NUM_CHANNELS; c++) s_out[c] = s_target[c];
        } else {
            float k = el / (float)rampMs;
            for (int c = 0; c < NUM_CHANNELS; c++) s_out[c] = s_rampFrom[c] + (s_target[c] - s_rampFrom[c]) * k;
        }
    } else {
        for (int c = 0; c < NUM_CHANNELS; c++) s_out[c] = s_target[c];
    }

    applyOutputs();
    computePower();
    updateFan(false);
    checkDailyRestart();
}

void getLampStatus(LampStatus& st) {
    LampLock lock;
    st.mode = s_mode;
    for (int c = 0; c < NUM_CHANNELS; c++) {
        st.v_target[c] = s_target[c];
        st.v_out[c] = s_out[c];
        st.duty[c] = getLedDuty(c);
    }
    st.power_pct = s_powerPct;
    st.fan_pct = s_fanPct;
    st.fan_on = s_fanOn;
    st.ramp_active = s_rampActive;
    st.time_valid = s_timeWasValid;
    st.minute_of_day = s_minuteOfDay;
}

const char* lampModeStr(LampMode m) {
    switch (m) {
        case MODE_PROGRAM: return "program";
        case MODE_TEST:    return "test";
        case MODE_NIGHT:   return "night";
    }
    return "?";
}

void enterManualMode(LampMode m) {
    if (m == MODE_PROGRAM) { exitManualMode(); return; }
    LampLock lock;
    for (int c = 0; c < NUM_CHANNELS; c++)
        s_manual[c] = (m == MODE_TEST) ? TEST_START_V : lampConfig().night_preset[c];
    s_mode = m;
    startRamp();
    LOG_INFO("Tryb %s", lampModeStr(m));
}

void setManualValues(const uint16_t v[NUM_CHANNELS]) {
    LampLock lock;
    if (s_mode == MODE_PROGRAM) return;
    for (int c = 0; c < NUM_CHANNELS; c++) s_manual[c] = v[c] > V_MAX ? V_MAX : v[c];
}

void getManualValues(uint16_t v[NUM_CHANNELS]) {
    LampLock lock;
    for (int c = 0; c < NUM_CHANNELS; c++) v[c] = s_manual[c];
}

void exitManualMode() {
    LampLock lock;
    if (s_mode == MODE_PROGRAM) return;
    s_mode = MODE_PROGRAM;
    startRamp();
    LOG_INFO("Powrót do programu");
}

void onActiveProgramChanged() {
    LampLock lock;
    if (s_mode == MODE_PROGRAM) startRamp();
}
