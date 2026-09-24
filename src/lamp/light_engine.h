#ifndef LIGHT_ENGINE_H
#define LIGHT_ENGINE_H

#include "lamp_types.h"

// ============================================================
// Silnik światła: czas lokalny → krzywa aktywnego programu (albo wartości
// ręczne trybu testowego/nocnego) → rampa → duty (γ, min_duty) → LEDC.
// Wentylator: feedforward z Σ power_frac · duty co 30 s, histereza ON/OFF.
// Restart dobowy 00:00–01:00 przy kanałach = 0, z gpio_hold.
// Wywołania z handlerów web — pod LampLock (funkcje biorą go same).
// ============================================================

enum LampMode : uint8_t { MODE_PROGRAM = 0, MODE_TEST, MODE_NIGHT };

struct LampStatus {
    LampMode mode;
    float    v_target[NUM_CHANNELS];  // setne % — cel (program albo suwaki)
    float    v_out[NUM_CHANNELS];     // setne % — po rampie
    uint32_t duty[NUM_CHANNELS];      // 0–16384
    float    power_pct;               // szacowana moc LED, % mocy lampy (może > 100)
    uint8_t  fan_pct;
    bool     fan_on;
    bool     ramp_active;
    bool     time_valid;
    uint16_t minute_of_day;
};

// bootWithHold: start po restarcie dobowym → od razu wartość programu, bez rampy.
void initLightEngine(bool bootWithHold);
void updateLightEngine();          // w loop(), co ~100 ms

void getLampStatus(LampStatus& st);
const char* lampModeStr(LampMode m);

// Tryb ręczny: suwaki startują od 50 % (test) albo presetu z FRAM (nocny), z rampą
void enterManualMode(LampMode m);
void setManualValues(const uint16_t v[NUM_CHANNELS]);   // live, bez rampy
void exitManualMode();                                   // powrót do programu, z rampą
void getManualValues(uint16_t v[NUM_CHANNELS]);

void onActiveProgramChanged();      // rampa do nowego programu

uint32_t vToDuty(uint8_t ch, float v);   // v w setnych %

#endif
