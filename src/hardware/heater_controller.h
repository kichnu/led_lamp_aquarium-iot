#ifndef HEATER_CONTROLLER_H
#define HEATER_CONTROLLER_H

#include <Arduino.h>

// SSR sterowanie grzałką (active LOW)
// Jeden zbiorczy SSR dla wszystkich grzałek równolegle

void initHeaterController();

void setHeaterOn();
void setHeaterOff();
bool isHeaterOn();

// Bezpieczne wyłączenie (wywołaj przed restartem)
void heaterEmergencyOff();

#endif // HEATER_CONTROLLER_H
