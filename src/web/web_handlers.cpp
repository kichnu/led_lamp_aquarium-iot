#include "web_handlers.h"
#include "web_server.h"
#include "html_pages.h"
#include "security/auth_manager.h"
#include "security/session_manager.h"
#include "security/rate_limiter.h"
#include "hardware/fram_controller.h"
#include "hardware/rtc_controller.h"
#include "network/wifi_manager.h"
#include "lamp/lamp_storage.h"
#include "lamp/program_store.h"
#include "lamp/light_engine.h"
#include "core/lamp_lock.h"
#include "core/sys_info.h"
#include "core/logging.h"
#include "config/config.h"
#include <ArduinoJson.h>
#include <WiFi.h>

// ===============================
// Pomocnicze
// ===============================

static void sendJson(AsyncWebServerRequest* request, JsonDocument& doc, int code = 200) {
    String out;
    serializeJson(doc, out);
    request->send(code, "application/json", out);
}

static void sendError(AsyncWebServerRequest* request, int code, const char* msg) {
    JsonDocument doc;
    doc["success"] = false;
    doc["error"] = msg;
    sendJson(request, doc, code);
}

static void sendOk(AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"success\":true}");
}

static String idToHex(uint64_t id) {
    char buf[17];
    snprintf(buf, sizeof(buf), "%08lX%08lX", (unsigned long)(id >> 32), (unsigned long)(id & 0xFFFFFFFF));
    return String(buf);
}

static bool hexToId(const String& s, uint64_t& id) {
    if (s.length() == 0 || s.length() > 16) return false;
    id = 0;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        uint8_t d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        id = (id << 4) | d;
    }
    return true;
}

static bool postParam(AsyncWebServerRequest* request, const char* name, String& out) {
    if (!request->hasParam(name, true)) return false;
    out = request->getParam(name, true)->value();
    return true;
}

// "t:v,t:v,..." → krzywa (walidacja w validateCurve)
static bool parseCurve(const String& s, ChannelCurve& c) {
    c.count = 0;
    int pos = 0;
    while (pos < (int)s.length()) {
        int comma = s.indexOf(',', pos);
        if (comma < 0) comma = s.length();
        int colon = s.indexOf(':', pos);
        if (colon < 0 || colon > comma || c.count >= MAX_POINTS) return false;
        long t = s.substring(pos, colon).toInt();
        long v = s.substring(colon + 1, comma).toInt();
        if (t < 0 || t > DAY_MIN || v < 0 || v > V_MAX) return false;
        c.pts[c.count].t = (uint16_t)t;
        c.pts[c.count].v = (uint16_t)v;
        c.count++;
        pos = comma + 1;
    }
    return validateCurve(c);
}

static const char* CH_PARAM[NUM_CHANNELS] = { "ch_a", "ch_b", "ch_c", "ch_d" };

// ===============================
// Status
// ===============================

static void handleStatus(AsyncWebServerRequest* request) {
    LampStatus st;
    getLampStatus(st);

    JsonDocument doc;
    doc["time"]        = getCurrentTimestamp();
    doc["time_valid"]  = st.time_valid;
    doc["time_src"]    = getRTCInfo();
    uint32_t ntpAge = getLastNtpSyncAgeS();
    if (ntpAge == UINT32_MAX) doc["ntp_age_s"] = nullptr; else doc["ntp_age_s"] = ntpAge;
    doc["minute"]      = st.minute_of_day;
    doc["mode"]        = lampModeStr(st.mode);
    doc["ramp"]        = st.ramp_active;
    doc["power_pct"]   = roundf(st.power_pct * 10) / 10;
    doc["fan_pct"]     = st.fan_pct;
    doc["fan_on"]      = st.fan_on;

    JsonArray ch = doc["channels"].to<JsonArray>();
    for (int c = 0; c < NUM_CHANNELS; c++) {
        JsonObject o = ch.add<JsonObject>();
        o["target"] = lroundf(st.v_target[c]);
        o["out"]    = lroundf(st.v_out[c]);
        o["duty"]   = st.duty[c];
    }

    uint16_t manual[NUM_CHANNELS];
    getManualValues(manual);
    JsonArray man = doc["manual"].to<JsonArray>();
    for (int c = 0; c < NUM_CHANNELS; c++) man.add(manual[c]);

    {
        LampLock lock;
        const Program& p = activeProgram();
        doc["active_id"]   = idToHex(p.id);
        doc["active_name"] = p.name;
        const SystemState& ss = sysState();
        doc["reset_reason"] = resetReasonStr(ss.last_reset_reason);
        JsonObject rc = doc["resets"].to<JsonObject>();
        rc["wdt"] = ss.cnt_wdt;
        rc["panic"] = ss.cnt_panic;
        rc["brownout"] = ss.cnt_brownout;
        rc["other"] = ss.cnt_other;
    }

    doc["uptime_s"]     = millis() / 1000;
    doc["heap"]         = ESP.getFreeHeap();
    doc["heap_min"]     = ESP.getMinFreeHeap();
    doc["heap_largest"] = ESP.getMaxAllocHeap();
    doc["fram_ok"]      = isFramInitialized();
    doc["rtc_ok"]       = isRTCWorking();
    doc["rtc_battery"]  = isBatteryIssueDetected();
    doc["wifi"]         = isWiFiConnected();
    doc["rssi"]         = WiFi.RSSI();
    doc["ip"]           = getLocalIP().toString();
    doc["device"]       = getDeviceID();
    doc["fw"]           = FW_VERSION;
    sendJson(request, doc);
}

// ===============================
// Programy
// ===============================

static void handlePrograms(AsyncWebServerRequest* request) {
    JsonDocument doc;
    LampLock lock;
    uint64_t activeId = activeProgram().id;
    doc["active_id"] = idToHex(activeId);
    doc["capacity"]  = FRAM_PROGRAM_SLOTS;
    JsonArray arr = doc["programs"].to<JsonArray>();
    bool activeInLibrary = false;
    for (uint8_t i = 0; i < catalogCount(); i++) {
        const CatalogEntry* e = catalogEntry(i);
        JsonObject o = arr.add<JsonObject>();
        o["id"]      = idToHex(e->id);
        o["name"]    = e->name;
        o["created"] = e->created_ts;
        o["factory"] = (e->flags & PROGRAM_FLAG_FACTORY) != 0;
        o["active"]  = e->id == activeId;
        if (e->id == activeId) activeInLibrary = true;
    }
    doc["active_in_library"] = activeInLibrary;   // false: program fabryczny tylko w RAM (brak FRAM)
    sendJson(request, doc);
}

static void programToJson(const Program& p, JsonDocument& doc) {
    doc["id"]     = idToHex(p.id);
    doc["parent"] = idToHex(p.parent_id);
    doc["name"]   = p.name;
    for (int c = 0; c < NUM_CHANNELS; c++) {
        char key[2] = { CHANNEL_NAMES[c], 0 };
        JsonArray arr = doc[key].to<JsonArray>();
        for (uint8_t i = 0; i < p.ch[c].count; i++) {
            JsonArray pt = arr.add<JsonArray>();
            pt.add(p.ch[c].pts[i].t);
            pt.add(p.ch[c].pts[i].v);
        }
    }
}

static void handleGetProgram(AsyncWebServerRequest* request) {
    uint64_t id;
    if (!request->hasParam("id") || !hexToId(request->getParam("id")->value(), id)) {
        sendError(request, 400, "missing id");
        return;
    }
    static Program p;   // ~830 B poza stosem, pod LampLock
    JsonDocument doc;
    LampLock lock;
    if (id == activeProgram().id) {
        p = activeProgram();          // także fabryczny tylko w RAM
    } else if (!loadProgram(id, p)) {
        sendError(request, 404, "program not found");
        return;
    }
    programToJson(p, doc);
    sendJson(request, doc);
}

static void handleSaveProgram(AsyncWebServerRequest* request) {
    String name;
    if (!postParam(request, "name", name)) { sendError(request, 400, "missing name"); return; }
    name.trim();
    if (name.length() == 0 || name.length() >= PROGRAM_NAME_LEN) {
        sendError(request, 400, "name length 1-23");
        return;
    }

    static Program p;
    LampLock lock;
    memset(&p, 0, sizeof(p));
    strncpy(p.name, name.c_str(), PROGRAM_NAME_LEN - 1);
    String parent;
    if (postParam(request, "parent", parent)) hexToId(parent, p.parent_id);

    for (int c = 0; c < NUM_CHANNELS; c++) {
        String s;
        if (!postParam(request, CH_PARAM[c], s) || !parseCurve(s, p.ch[c])) {
            char msg[32];
            snprintf(msg, sizeof(msg), "invalid curve %c", CHANNEL_NAMES[c]);
            sendError(request, 400, msg);
            return;
        }
    }

    uint64_t newId = 0;
    ProgramError e = saveNewProgram(p, newId);
    if (e != PROG_OK) { sendError(request, e == PROG_ERR_FULL ? 409 : 500, programErrorStr(e)); return; }

    JsonDocument doc;
    doc["success"] = true;
    doc["id"] = idToHex(newId);
    sendJson(request, doc);
}

static bool idParam(AsyncWebServerRequest* request, uint64_t& id) {
    String s;
    return postParam(request, "id", s) && hexToId(s, id);
}

static void handleActivateProgram(AsyncWebServerRequest* request) {
    uint64_t id;
    if (!idParam(request, id)) { sendError(request, 400, "missing id"); return; }
    ProgramError e = activateProgram(id);
    if (e != PROG_OK) { sendError(request, e == PROG_ERR_NOT_FOUND ? 404 : 500, programErrorStr(e)); return; }
    onActiveProgramChanged();
    sendOk(request);
}

static void handleDeleteProgram(AsyncWebServerRequest* request) {
    uint64_t id;
    if (!idParam(request, id)) { sendError(request, 400, "missing id"); return; }
    ProgramError e = deleteProgram(id);
    if (e != PROG_OK) {
        sendError(request, e == PROG_ERR_NOT_FOUND ? 404 : (e == PROG_ERR_ACTIVE ? 409 : 500), programErrorStr(e));
        return;
    }
    sendOk(request);
}

// ===============================
// Tryb ręczny (test / nocny)
// ===============================

static void sendManualValues(AsyncWebServerRequest* request) {
    uint16_t v[NUM_CHANNELS];
    getManualValues(v);
    JsonDocument doc;
    doc["success"] = true;
    JsonArray arr = doc["values"].to<JsonArray>();
    for (int c = 0; c < NUM_CHANNELS; c++) arr.add(v[c]);
    sendJson(request, doc);
}

static void handleManualEnter(AsyncWebServerRequest* request) {
    String mode;
    if (!postParam(request, "mode", mode)) { sendError(request, 400, "missing mode"); return; }
    if (mode == "test") enterManualMode(MODE_TEST);
    else if (mode == "night") enterManualMode(MODE_NIGHT);
    else { sendError(request, 400, "mode: test|night"); return; }
    sendManualValues(request);
}

static void handleManualSet(AsyncWebServerRequest* request) {
    uint16_t v[NUM_CHANNELS];
    getManualValues(v);
    for (int c = 0; c < NUM_CHANNELS; c++) {
        String s;
        if (postParam(request, CH_PARAM[c], s)) {
            long x = s.toInt();
            v[c] = (uint16_t)(x < 0 ? 0 : (x > V_MAX ? V_MAX : x));
        }
    }
    setManualValues(v);
    sendOk(request);
}

static void handleManualExit(AsyncWebServerRequest* request) {
    exitManualMode();
    sendOk(request);
}

// ===============================
// Konfiguracja (sekcja "settings")
// ===============================

static void handleGetConfig(AsyncWebServerRequest* request) {
    JsonDocument doc;
    LampLock lock;
    const LampConfig& lc = lampConfig();
    doc["ramp_s"]      = lc.ramp_s;
    doc["ramp_min"]    = RAMP_S_MIN;
    doc["ramp_max"]    = RAMP_S_MAX;
    doc["fan_on_pct"]  = lc.fan_on_pct;
    doc["fan_off_pct"] = lc.fan_off_pct;
    JsonArray night = doc["night"].to<JsonArray>();
    for (int c = 0; c < NUM_CHANNELS; c++) night.add(lc.night_preset[c]);
    JsonArray chs = doc["channels"].to<JsonArray>();
    for (uint8_t c = 0; c < NUM_CHANNELS; c++) {
        const ChannelConfig& cc = channelConfig(c);
        JsonObject o = chs.add<JsonObject>();
        o["power_frac"] = cc.power_frac;
        o["gamma"]      = cc.gamma;
        o["min_duty"]   = cc.min_duty;
        char label[sizeof(cc.label) + 1];
        memcpy(label, cc.label, sizeof(cc.label));
        label[sizeof(cc.label)] = '\0';
        o["label"] = label;
    }
    sendJson(request, doc);
}

static void handleSetConfig(AsyncWebServerRequest* request) {
    static const char* NIGHT[NUM_CHANNELS] = { "night_a", "night_b", "night_c", "night_d" };
    static const char* PF[NUM_CHANNELS]    = { "pf_a", "pf_b", "pf_c", "pf_d" };
    static const char* GAMMA[NUM_CHANNELS] = { "gamma_a", "gamma_b", "gamma_c", "gamma_d" };
    static const char* MIND[NUM_CHANNELS]  = { "min_duty_a", "min_duty_b", "min_duty_c", "min_duty_d" };
    static const char* LABEL[NUM_CHANNELS] = { "label_a", "label_b", "label_c", "label_d" };

    LampLock lock;
    LampConfig lc = lampConfig();
    String s;
    if (postParam(request, "ramp_s", s)) {
        long r = s.toInt();
        if (r < RAMP_S_MIN || r > RAMP_S_MAX) { sendError(request, 400, "ramp_s 3-30"); return; }
        lc.ramp_s = r;
    }
    if (postParam(request, "fan_on_pct", s))  lc.fan_on_pct  = constrain(s.toInt(), 0, 100);
    if (postParam(request, "fan_off_pct", s)) lc.fan_off_pct = constrain(s.toInt(), 0, 100);
    if (lc.fan_off_pct > lc.fan_on_pct) { sendError(request, 400, "fan_off_pct must be <= fan_on_pct"); return; }
    for (int c = 0; c < NUM_CHANNELS; c++)
        if (postParam(request, NIGHT[c], s)) lc.night_preset[c] = constrain(s.toInt(), 0, V_MAX);

    ChannelConfig cc[NUM_CHANNELS];
    bool chChanged[NUM_CHANNELS] = { false };
    for (uint8_t c = 0; c < NUM_CHANNELS; c++) {
        cc[c] = channelConfig(c);
        if (postParam(request, PF[c], s))    { cc[c].power_frac = constrain(s.toInt(), 0, 10000); chChanged[c] = true; }
        if (postParam(request, MIND[c], s))  { cc[c].min_duty = constrain(s.toInt(), 0, (long)PWM_MAX_DUTY); chChanged[c] = true; }
        if (postParam(request, GAMMA[c], s)) {
            float g = s.toFloat();
            if (g < 0.2f || g > 4.0f) { sendError(request, 400, "gamma 0.2-4.0"); return; }
            cc[c].gamma = g;
            chChanged[c] = true;
        }
        if (postParam(request, LABEL[c], s)) {
            memset(cc[c].label, 0, sizeof(cc[c].label));
            strncpy(cc[c].label, s.c_str(), sizeof(cc[c].label) - 1);
            chChanged[c] = true;
        }
    }

    bool ok = true;
    if (memcmp(&lc, &lampConfig(), sizeof(lc)) != 0) {
        lampConfig() = lc;
        ok &= saveLampConfig();
    }
    for (uint8_t c = 0; c < NUM_CHANNELS; c++) {
        if (!chChanged[c]) continue;
        channelConfig(c) = cc[c];
        ok &= saveChannelConfig(c);
    }
    if (!ok) { sendError(request, 500, "FRAM error"); return; }
    LOG_INFO("Config zapisany: ramp=%us fan=%u/%u%%", lc.ramp_s, lc.fan_on_pct, lc.fan_off_pct);
    sendOk(request);
}

// ===============================
// Rejestracja
// ===============================

void registerLampHandlers(AsyncWebServer& server) {
    server.on("/api/status",           HTTP_GET,  requireAuth(handleStatus));
    server.on("/api/programs",         HTTP_GET,  requireAuth(handlePrograms));
    server.on("/api/program",          HTTP_GET,  requireAuth(handleGetProgram));
    server.on("/api/program-save",     HTTP_POST, requireAuth(handleSaveProgram));
    server.on("/api/program-activate", HTTP_POST, requireAuth(handleActivateProgram));
    server.on("/api/program-delete",   HTTP_POST, requireAuth(handleDeleteProgram));
    server.on("/api/manual-enter",     HTTP_POST, requireAuth(handleManualEnter));
    server.on("/api/manual-set",       HTTP_POST, requireAuth(handleManualSet));
    server.on("/api/manual-exit",      HTTP_POST, requireAuth(handleManualExit));
    server.on("/api/config",           HTTP_GET,  requireAuth(handleGetConfig));
    server.on("/api/config",           HTTP_POST, requireAuth(handleSetConfig));
}

// ============================================================
// Auth + strony statyczne
// ============================================================

// Strona z flasha bez kopii do heapu: send(code, type, const char*) robi z niej
// String (~50 KB na każde otwarcie) — na C3 bez PSRAM to 1/3 wolnego heapu.
// Wzorzec z dolewki (top_off_water_new-iot, ESP32-C3).
static void sendPage(AsyncWebServerRequest* request, const char* html) {
    AsyncWebServerResponse* response =
        request->beginResponse(200, "text/html", (const uint8_t*)html, strlen(html));
    request->send(response);
}

void handleDashboard(AsyncWebServerRequest* request) {
    sendPage(request, getDashboardHtml());
}

void handleLoginPage(AsyncWebServerRequest* request) {
    sendPage(request, getLoginHtml());
}

void handleLogin(AsyncWebServerRequest* request) {
    if (!request->hasParam("password", true)) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"missing password\"}");
        return;
    }

    IPAddress clientIP = resolveClientIP(request);
    if (isRateLimited(clientIP)) {
        recordFailedAttempt(clientIP);
        request->send(429, "application/json", "{\"success\":false,\"error\":\"rate limited\"}");
        return;
    }

    String password = request->getParam("password", true)->value();
    if (!verifyPassword(password)) {
        recordFailedAttempt(clientIP);
        request->send(401, "application/json", "{\"success\":false,\"error\":\"invalid password\"}");
        return;
    }

    recordRequest(clientIP);
    String token = createSession(clientIP);

    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", "{\"success\":true}");
    response->addHeader("Set-Cookie", "session_token=" + token + "; Path=/; HttpOnly");
    request->send(response);
}

void handleLogout(AsyncWebServerRequest* request) {
    if (request->hasHeader("Cookie")) {
        String cookie = request->getHeader("Cookie")->value();
        int tokenStart = cookie.indexOf("session_token=");
        if (tokenStart != -1) {
            tokenStart += 14;
            int tokenEnd = cookie.indexOf(";", tokenStart);
            if (tokenEnd == -1) tokenEnd = cookie.length();
            destroySession(cookie.substring(tokenStart, tokenEnd));
        }
    }
    request->send(200, "application/json", "{\"success\":true}");
}

void handleHealth(AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}
