#ifndef FAN_CONTROLLER_H
#define FAN_CONTROLLER_H

#include <Arduino.h>

// Iduino ST1168 MOSFET PWM — sterowanie wentylatorami DC 5–24V
// LEDC channel 0, 25kHz, 8-bit rozdzielczość (0–255)

void initFanController();

void setFanSpeed(uint8_t pct);  // 0 = stop, 100 = pełna prędkość
uint8_t getFanSpeedPct();

// Oblicz prędkość na podstawie temperatury i konfiguracji, przy założeniu że fan
// jest już aktywny (decyzję ON/OFF podejmuje TempAlgorithm::_updateFan, latch).
// Zwraca 0–100: krzywa liniowa [cool_start_abs, cool_full_abs] z podłogą min_pct
// (poniżej min_pct wentylator DC się nie rusza / jest nieefektywny).
uint8_t calcFanSpeed(float temp, float cool_start_abs, float cool_full_abs, uint8_t min_pct);

void fanEmergencyOff();

#endif // FAN_CONTROLLER_H
