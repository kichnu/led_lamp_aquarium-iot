#ifndef CONFIG_H
#define CONFIG_H

#include <IPAddress.h>

// ================= DEBUG =================
#define ENABLE_FULL_LOGGING true
#define ENABLE_SERIAL_DEBUG true

// ================= CREDENTIAL ACCESSORS (via credentials_manager) =================
extern const char* WIFI_SSID;
extern const char* WIFI_PASSWORD;
extern const char* ADMIN_PASSWORD_HASH;
extern const char* DEVICE_ID;
extern const IPAddress TRUSTED_PROXY_IP;

// ================= FIRMWARE =================
#define FW_NAME     "RL90 Lamp"
#define FW_VERSION  "0.1.0"

// ================= SECURITY CONSTANTS =================
const unsigned long SESSION_TIMEOUT_MS      = 1800000;
const unsigned long RATE_LIMIT_WINDOW_MS    = 1000;
const int           MAX_REQUESTS_PER_SECOND = 5;
const int           MAX_FAILED_ATTEMPTS     = 10;
const unsigned long BLOCK_DURATION_MS       = 60000;

// ================= DYNAMIC CREDENTIAL ACCESSORS =================
#include "credentials_manager.h"

#define WIFI_SSID_DYNAMIC            getWiFiSSID()
#define WIFI_PASSWORD_DYNAMIC        getWiFiPassword()
#define ADMIN_PASSWORD_HASH_DYNAMIC  getAdminPasswordHash()
#define DEVICE_ID_DYNAMIC            getDeviceID()


#endif // CONFIG_H
