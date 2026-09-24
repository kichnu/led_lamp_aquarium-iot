#ifndef LAMP_LOCK_H
#define LAMP_LOCK_H

#include <Arduino.h>

// ============================================================
// Jeden rekurencyjny mutex na stan lampy i magistralę I2C.
// Handlery ESPAsyncWebServer działają w tasku AsyncTCP, silnik światła w loop() —
// oba sięgają do FRAM (Wire: adres i odczyt to dwie transakcje, nie mogą się
// przeplatać) i do aktywnego programu / konfiguracji w RAM.
// Użycie: { LampLock lock; ... } — zwolnienie przy wyjściu z zakresu.
// ============================================================

void initLampLock();
void lampLock();
void lampUnlock();

struct LampLock {
    LampLock()  { lampLock(); }
    ~LampLock() { lampUnlock(); }
    LampLock(const LampLock&) = delete;
    LampLock& operator=(const LampLock&) = delete;
};

#endif
