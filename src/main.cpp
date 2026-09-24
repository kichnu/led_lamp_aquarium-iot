#include <Arduino.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include "core/logging.h"
#include "core/lamp_lock.h"
#include "core/sys_info.h"
#include "config/config.h"
#include "config/credentials_manager.h"
#include "hardware/hardware_pins.h"
#include "hardware/fram_controller.h"
#include "hardware/rtc_controller.h"
#include "hardware/pwm_output.h"
#include "lamp/lamp_storage.h"
#include "lamp/program_store.h"
#include "lamp/light_engine.h"
#include "network/wifi_manager.h"
#include "security/auth_manager.h"
#include "security/session_manager.h"
#include "security/rate_limiter.h"
#include "web/web_server.h"
#include "provisioning/prov_detector.h"
#include "provisioning/ap_core.h"
#include "provisioning/ap_server.h"
#include "provisioning/prov_config.h"

#define ENGINE_TICK_MS 100

static bool g_otaActive = false;

void setup() {
    // Najpierw LEDC z 0 % — wejścia Hi7001 bez sterowania świecą 100 % (pull-up).
    // Skraca błysk przy starcie i trzyma lampę ciemną w provisioningu.
    bool bootWithHold = initPwmOutputs();

    initLampLock();
    initLogging();

    LOG_INFO("====================================");
    LOG_INFO("=== %s v%s — XIAO ESP32-C3 ===", FW_NAME, FW_VERSION);
    LOG_INFO("Flash: %d  Heap: %d  Reset: %s%s", ESP.getFlashChipSize(), ESP.getFreeHeap(),
             resetReasonStr(esp_reset_reason()), bootWithHold ? " (z hold)" : "");

    initI2CBus();

    if (checkProvisioningButton()) {
        LOG_INFO("=== PROVISIONING MODE ===");
        if (!initFRAM()) {
            LOG_INFO("WARNING: FRAM init failed — credentials may not save");
            delay(2000);
        }
        if (!startAccessPoint() || !startDNSServer()) {
            LOG_INFO("FATAL: AP start failed — halting");
            while (1) delay(1000);
        }
        LOG_INFO("SSID: %s  pass: %s", PROV_AP_SSID, PROV_AP_PASSWORD);
        LOG_INFO("URL: http://%s", getAPIPAddress().toString().c_str());
        if (!startWebServer()) {
            while (1) delay(1000);
        }
        runProvisioningLoop();  // nie wraca
    }

    // === PRODUCTION MODE ===
    initializeRTC();            // zegar systemowy z DS3231 — przed FRAM (init_ts, created_ts)
    initFRAM();

    bool blankFram = false;
    initLampStorage(blankFram);
    recordResetReason();
    initProgramStore(blankFram);
    initLightEngine(bootWithHold);

    bool credentials_loaded = initCredentialsManager();
    LOG_INFO("Device ID: %s", credentials_loaded ? getDeviceID() : "FALLBACK");

    initWiFi();                 // bez blokowania, połączenie w tle
    startNTP();

    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.setRebootOnSuccess(false);   // restart sami, z A–D trzymanymi w LOW (bez błysku 100 %)
    ArduinoOTA.onStart([]() {
        g_otaActive = true;
        LOG_INFO("=== OTA START ===");
    });
    ArduinoOTA.onProgress([](unsigned int, unsigned int) {
        esp_task_wdt_reset();   // zapis flash w callbacku — bez tego TWDT resetuje w trakcie OTA (dozownik)
    });
    ArduinoOTA.onEnd([]() {
        LOG_INFO("=== OTA OK — restart ===");
        restartWithLedsHeldLow();
    });
    ArduinoOTA.onError([](ota_error_t error) {
        g_otaActive = false;
        LOG_WARNING("=== OTA ERROR (%d) ===", (int)error);
    });
    ArduinoOTA.begin();
    LOG_INFO("ArduinoOTA ready (port 3232)");
    startLogServer();

    initAuthManager();
    initSessionManager();
    initRateLimiter();
    initWebServer();

    LOG_INFO("Time: %s (%s)", getCurrentTimestamp().c_str(), getRTCInfo().c_str());

    // loop() pod Task WDT (domyślnie 5 s) — zawieszenie z oddawaniem CPU (np. pętla
    // retry I2C z delay()) zostawiłoby PWM na ostatniej wartości bez resetu
    enableLoopWDT();
    LOG_INFO("=== Init complete ===");
}

void loop() {
    ArduinoOTA.handle();
    if (g_otaActive) return;    // w trakcie OTA tylko handle() — LEDC trzyma ostatnie wartości

    unsigned long now = millis();

    static unsigned long lastEngine = 0;
    if (now - lastEngine >= ENGINE_TICK_MS) {
        lastEngine = now;
        updateLightEngine();    // w tym restart dobowy 00:00–01:00
    }

    static unsigned long lastTick = 0;
    if (now - lastTick >= 100) {
        lastTick = now;
        updateSessionManager();
        updateRateLimiter();
        updateWiFi();
        updateLogServer();
        updateRTCSync();
        updateProvisioningButtonLog();
    }

    static unsigned long lastHeapLog = 0;
    if (now - lastHeapLog >= 3600000UL) {
        lastHeapLog = now;
        LOG_INFO("Heap: free=%u min=%u largest=%u", ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    }

    delay(10);
}
