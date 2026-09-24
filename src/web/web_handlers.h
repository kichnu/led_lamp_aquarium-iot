#ifndef WEB_HANDLERS_H
#define WEB_HANDLERS_H

#include <ESPAsyncWebServer.h>

// ============================================================
// API — RL90 Lamp. POST: application/x-www-form-urlencoded, odpowiedzi JSON.
// ============================================================
// Auth (bez zmian z termostatu):
//   POST /api/login, POST /api/logout, GET /api/health
//
// Lampa:
//   GET  /api/status             stan kanałów, wentylatora, czasu, diagnostyka
//   GET  /api/programs           katalog biblioteki (kolejność dowolna — GUI sortuje)
//   GET  /api/program?id=<hex>   punkty 4 kanałów {"A":[[t,v100],...],...}
//   POST /api/program-save       name, parent (hex, opcj.), ch_a..ch_d "t:v,t:v,..." → nowy id
//   POST /api/program-activate   id
//   POST /api/program-delete     id (tombstone; aktywnego nie można)
//   POST /api/manual-enter       mode=test|night
//   POST /api/manual-set         ch_a..ch_d (setne %)
//   POST /api/manual-exit
//   GET  /api/config             LAMP_CONFIG + CHANNEL_CONFIG
//   POST /api/config             pola opcjonalne: ramp_s, fan_on_pct, fan_off_pct,
//                                night_a..d, pf_a..d (‱), gamma_a..d, min_duty_a..d, label_a..d
// ============================================================

void registerLampHandlers(AsyncWebServer& server);

void handleDashboard(AsyncWebServerRequest* request);
void handleLoginPage(AsyncWebServerRequest* request);
void handleLogin(AsyncWebServerRequest* request);
void handleLogout(AsyncWebServerRequest* request);
void handleHealth(AsyncWebServerRequest* request);

#endif // WEB_HANDLERS_H
