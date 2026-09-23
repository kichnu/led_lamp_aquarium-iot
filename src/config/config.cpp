#include "config.h"
#include "core/logging.h"
#include "hardware/fram_controller.h"

// Placeholder credentials — zastępowane przez FRAM po provisioning
const char* WIFI_SSID          = "KiG_2.4_IOT";
const char* WIFI_PASSWORD      = "*qY4I@5&*%0lK1Q$U6UV7^S";
const char* ADMIN_PASSWORD_HASH = nullptr;
const char* DEVICE_ID          = "UNCONFIGURED_DEVICE";
const IPAddress TRUSTED_PROXY_IP(10, 99, 0, 1);

// ============== SYSTEM DISABLE/ENABLE ==============

bool systemDisabled = false;  // boot zawsze w trybie normalnym — Service Mode tylko z GUI
static unsigned long s_system_disabled_ms = 0;

void setSystemState(bool enabled) {
    systemDisabled = !enabled;
    s_system_disabled_ms = enabled ? 0 : millis();
    if (enabled) {
        LOG_INFO("=== SYSTEM ENABLED — algorytm wznawia pracę ===");
    } else {
        LOG_INFO("=== SYSTEM DISABLED — algorytm wstrzymany ===");
    }
}

bool isSystemDisabled() {
    return systemDisabled;
}

void checkSystemAutoEnable() {
    if (systemDisabled && s_system_disabled_ms > 0) {
        if (millis() - s_system_disabled_ms >= SYSTEM_AUTO_ENABLE_MS) {
            s_system_disabled_ms = 0;
            systemDisabled = false;
            LOG_INFO("=== SYSTEM AUTO-ENABLED (timeout) ===");
        }
    }
}

// GUI countdown (btnSystemToggle) — odejmowanie unsigned long, bezpieczne
// przy przepełnieniu millis() tak samo jak w checkSystemAutoEnable() (patrz
// tam); w praktyce i tak nieistotne, bo urządzenie restartuje się co dobę.
uint32_t getSystemAutoEnableRemainingS() {
    if (!systemDisabled || s_system_disabled_ms == 0) return 0;
    unsigned long elapsed = millis() - s_system_disabled_ms;
    if (elapsed >= SYSTEM_AUTO_ENABLE_MS) return 0;
    return (uint32_t)((SYSTEM_AUTO_ENABLE_MS - elapsed) / 1000UL);
}

// ============== FRAM INIT ==============

void initSystemFRAM() {
    if (!initFRAM()) {
        LOG_ERROR("FRAM initialization failed!");
    } else {
        LOG_INFO("FRAM ready (SPI)");
    }
}
