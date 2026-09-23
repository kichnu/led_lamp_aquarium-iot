#ifndef WEB_HANDLERS_H
#define WEB_HANDLERS_H

#include <ESPAsyncWebServer.h>

// ============================================================
// API endpoints — Thermo Control
// ============================================================
// Auth (bez zmian z ATO):
//   POST /api/login, POST /api/logout, GET /api/health
//
// Thermo:
//   GET  /api/status
//   GET  /api/history-hourly
//   GET  /api/history-daily
//   GET  /api/alarm-events
//   GET  /api/thermo-config
//   POST /api/thermo-config
//   POST /api/mute-alarm
//   POST /api/reset-sensor-fault
//   POST /api/system-toggle
//   GET  /api/energy
//   POST /api/energy-reset-day
//   POST /api/energy-reset-total  (kasowanie licznika total — gating PIN po stronie GUI, .lockable-btn)
//   POST /api/verify-pin          (pin) — lock-PIN edycji GUI, drugi zamek NA edycję, nie zamiennik loginu
// ============================================================

void registerThermoHandlers(AsyncWebServer& server);

void handleStatus(AsyncWebServerRequest* request);
void handleHourlyHistory(AsyncWebServerRequest* request);
void handleDailyHistory(AsyncWebServerRequest* request);
void handleAlarmEvents(AsyncWebServerRequest* request);
void handleGetThermoConfig(AsyncWebServerRequest* request);
void handleSetThermoConfig(AsyncWebServerRequest* request);
void handleMuteAlarm(AsyncWebServerRequest* request);

// SENSOR_FAULT to alarm krytyczny (latch) — nie wraca sam, wymaga tego wywołania
void handleResetSensorFault(AsyncWebServerRequest* request);
void handleSystemToggle(AsyncWebServerRequest* request);
void handleEnergy(AsyncWebServerRequest* request);
void handleEnergyResetDay(AsyncWebServerRequest* request);
void handleEnergyResetTotal(AsyncWebServerRequest* request);

// Test: ręczne sterowanie wentylatorem z GUI (bypass algorytmu) — POST pct=0-100
void handleTestFan(AsyncWebServerRequest* request);

// Test: ręczne sterowanie grzałką z GUI (bypass algorytmu) — POST on=0|1
void handleTestHeater(AsyncWebServerRequest* request);

// Test: wypełnia/czyści bufory hourly/daily/alarm losowymi danymi demo — do
// pracy nad GUI przed zebraniem prawdziwej historii przez urządzenie.
void handleTestSeedHistory(AsyncWebServerRequest* request);
void handleTestClearHistory(AsyncWebServerRequest* request);

// Lock-PIN edycji GUI (osobny od hasła logowania) — drugi zamek na zapisy
void handleVerifyPin(AsyncWebServerRequest* request);

// ============================================================
// Auth + strony statyczne (bez zmian z ATO)
// ============================================================
void handleDashboard(AsyncWebServerRequest* request);
void handleLoginPage(AsyncWebServerRequest* request);
void handleLogin(AsyncWebServerRequest* request);
void handleLogout(AsyncWebServerRequest* request);
void handleHealth(AsyncWebServerRequest* request);

#endif // WEB_HANDLERS_H
