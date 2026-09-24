#ifndef RTC_CONTROLLER_H
#define RTC_CONTROLLER_H

#include <Arduino.h>

// ============================================================
// Czas: DS3231 trzyma UTC, zegar systemowy ESP32 jest źródłem dla reszty
// firmware (time()/localtime_r z POLAND_TZ). Przy starcie zegar systemowy
// ustawiany z DS3231; SNTP działa w tle (lwIP, resync co godzinę) i po każdej
// synchronizacji wynik trafia do DS3231 — bez blokującego czekania na NTP
// (termostat czekał do 20 s, przy lampie to przekracza 5 s TWDT).
// ============================================================

void initializeRTC();           // po initI2CBus(); ustawia TZ i zegar systemowy z DS3231
void startNTP();                // po WiFi.mode(STA); SNTP w tle
void updateRTCSync();           // w loop(): zapis czasu z NTP do DS3231 po synchronizacji

bool isTimeValid();             // zegar systemowy ma sensowny czas (z DS3231 albo NTP)
bool isRTCWorking();            // DS3231 obecny i z poprawnym czasem
bool isBatteryIssueDetected();  // DS3231 zgłosił utratę zasilania (OSF)
unsigned long getUnixTimestamp();   // UTC
String getCurrentTimestamp();       // lokalny "YYYY-MM-DD HH:MM:SS"
String getRTCInfo();
uint32_t getLastNtpSyncAgeS();      // UINT32_MAX = nigdy

#endif
