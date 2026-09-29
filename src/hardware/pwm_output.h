#ifndef PWM_OUTPUT_H
#define PWM_OUTPUT_H

#include <Arduino.h>

// ============================================================
// LEDC: A–D + FAN na wspólnym timerze 500 Hz / 14 bit (#define w lamp_types.h).
// Sterownik IDF (driver/ledc.h) wprost, jak w pwm_test/ — niezależnie od
// wersji API ledc* rdzenia Arduino.
// ============================================================

// Konfiguruje LEDC z wypełnieniem 0 i dopiero potem zwalnia gpio_hold na A–D.
// Zwraca true, gdy start nastąpił po planowym restarcie z hold (restart dobowy).
bool initPwmOutputs();

void     setLedDuty(uint8_t ch, uint32_t duty);   // 0–16384, obcinane do 16383
uint32_t getLedDuty(uint8_t ch);
void     setFanDuty(uint32_t duty);               // 0–16384, obcinane do 16383
uint32_t getFanDuty();

void     allOutputsOff();                         // A–D i FAN = 0 (OTA, awaria)

// A–D na stałe LOW + gpio_hold_en, znacznik w RTC_NOINIT, ESP.restart(). Nie wraca.
// Restart dobowy i po OTA — po starcie od razu wartość programu, bez błysku i rampy.
void     restartWithLedsHeldLow();

#endif
