#ifndef ENERGY_METER_H
#define ENERGY_METER_H

#include <Arduino.h>
#include "algorithm_config.h"

// Miernik energii — wejście impulsów (open-collector przez optocoupler PC817)
// GPIO ENERGY_PULSE_PIN: INPUT_PULLUP, ISR FALLING
// Konfiguracja: pulses_per_kwh w ThermoConfig (domyślnie 1000 imp/kWh)

void initEnergyMeter(uint16_t pulses_per_kwh);
void updateEnergyMeter();               // wywołuj co loop() — obsługuje zapis do FRAM

// Odłącza/przywraca ISR impulsów energii — używane wokół OTA erase/write:
// szum EMI na GPIO9 (rząd 700 fałszywych imp/s, patrz pamięć projektu) strzela
// ISR-em na tym samym rdzeniu, który w tym momencie polluje status rejestru
// flash przy kasowaniu sektora; przy dużej częstotliwości przerwań ta pętla
// pollingu może przekroczyć swój timeout i wywołać abort() w esp_flash_erase_region.
void pauseEnergyPulseInterrupt();
void resumeEnergyPulseInterrupt();

void setEnergyPulsesPerKwh(uint16_t ppk);

float    getEnergyKwhTotal();           // narastające kWh od ostatniego resetu
float    getEnergyKwhToday();           // kWh w bieżącej dobie
uint32_t getEnergyPulseCount();         // surowy licznik impulsów
float    getPowerWatt();                // moc chwilowa [W] z czasu między impulsami
uint32_t getEnergyResetTimestamp();     // Unix UTC ostatniego kasowania licznika total (0 = nigdy)

void     energyResetDayCounter();       // reset licznika dobowego (co północ)
void     energyResetTotalCounter();     // kasowanie licznika total z przycisku GUI (PIN po stronie web_handlers/GUI) — zapisuje też timestamp
void     energyLoadFromFRAM();
void     energySaveToFRAM();

#endif // ENERGY_METER_H
