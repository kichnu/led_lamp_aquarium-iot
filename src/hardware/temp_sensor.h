#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <Arduino.h>

// Sensirion STS35-DIS — I2C, adres 0x4B (ADDR=VDD)
// Single-shot high repeatability: ~15ms konwersja
// Pomiar wyzwalany co TEMP_MEASURE_INTERVAL_S (15s)
// Protokół: zapis 2B komendy → wait 15ms → odczyt 3B (temp_H, temp_L, CRC)
// T[°C] = 175 * raw / 65535 - 45

void     initTempSensor();
void     updateTempSensor();        // wywołuj co TEMP_MEASURE_INTERVAL_S

bool     isTempValid();             // false = brak czujnika lub błąd CRC (po wyczerpaniu retry)
float    getTemperature();          // ostatni ważny odczyt [°C] + offset kalibracji
int16_t  getTemperatureX10();       // temp × 10 dla FRAM (250 = 25.0°C)
uint8_t  getSensorFaultCount();     // liczba kolejnych w pełni nieudanych CYKLI (reset po valid read)

#endif // TEMP_SENSOR_H
