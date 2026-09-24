#include "auth_manager.h"
#include "../config/config.h"
#include "../core/logging.h"
#include <mbedtls/md.h>
#include "../config/credentials_manager.h"

void initAuthManager() {
    LOG_INFO("");
    LOG_INFO("Authentication manager initialized");

    LOG_INFO("Using %s admin credentials", areCredentialsLoaded() ? "FRAM" : "fallback");
    if (areCredentialsLoaded()) {
        LOG_INFO("");
        LOG_INFO("Using FRAM admin credentials");
    } else {
        LOG_WARNING("");
        LOG_WARNING("====================================");
        LOG_WARNING("⚠️ NO FRAM CREDENTIALS - Web authentication DISABLED!");
        LOG_WARNING("⚠️ Use Captive Portal to configure credentials!");
        LOG_WARNING("");
    }
}

// Etap bench-test: zezwól na całą prywatną sieć lokalną (RFC1918) zamiast
// wąskiej whitelisty pojedynczych IP — do doprecyzowania przy hardeningu.
bool isIPAllowed(IPAddress ip) {
    if (ip[0] == 10) return true;                                  // 10.0.0.0/8
    if (ip[0] == 192 && ip[1] == 168) return true;                  // 192.168.0.0/16
    if (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) return true;     // 172.16.0.0/12
    return false;
}

String hashPassword(const String& password) {
    byte hash[32];
    
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const unsigned char*)password.c_str(), password.length());
    mbedtls_md_finish(&ctx, hash);
    mbedtls_md_free(&ctx);
    
    String hashString = "";
    for (int i = 0; i < 32; i++) {
        char str[3];
        sprintf(str, "%02x", (int)hash[i]);
        hashString += str;
    }
    
    return hashString;
}

bool verifyPassword(const String& password) {
    if (!areCredentialsLoaded()) {
        LOG_ERROR("");
        LOG_ERROR("====================================");
        LOG_ERROR("🔒 Authentication BLOCKED - No FRAM credentials loaded!");
        LOG_ERROR("🔧 Use Captive Portal to configure credentials first:");
        LOG_ERROR("   1. Hold button for 5 seconds during boot");
        LOG_ERROR("   2. Connect to RL90-LAMP-SETUP WiFi");
        LOG_ERROR("   3. Configure credentials in browser");
        LOG_ERROR("====================================");
        return false;  // Force FRAM setup!
    }
    
    String inputHash = hashPassword(password);
    String expectedHash = String(getAdminPasswordHash());
    
    if (expectedHash.length() == 0 || expectedHash == "NO_AUTH_REQUIRES_FRAM_PROGRAMMING") {
        LOG_ERROR("");
        LOG_ERROR("Invalid admin hash from FRAM - check credential programming");
        return false;
    }
    
    bool valid = (inputHash == expectedHash);

    if (valid) {
        LOG_INFO("");
        LOG_INFO("✅ Password verification successful (FRAM credentials)");
    } else {
        LOG_WARNING("");
        LOG_WARNING("====================================");
        LOG_WARNING("❌ Password verification failed");
        LOG_WARNING("💡 Verify admin password was correctly programmed to FRAM"); 
        LOG_WARNING("====================================");
    }
    return valid;
}

// ============================================
// TRUSTED PROXY - VPS reverse proxy
// Requests from this IP bypass authentication
// ============================================

bool isTrustedProxy(IPAddress ip) {
    return ip == TRUSTED_PROXY_IP;
}

IPAddress resolveClientIP(AsyncWebServerRequest* request) {
    IPAddress sourceIP = request->client()->remoteIP();
    if (isTrustedProxy(sourceIP) && request->hasHeader("X-Forwarded-For")) {
        String xff = request->getHeader("X-Forwarded-For")->value();
        int comma = xff.indexOf(',');
        String clientStr = (comma > 0) ? xff.substring(0, comma) : xff;
        clientStr.trim();
        IPAddress realIP;
        if (realIP.fromString(clientStr)) {
            LOG_INFO("Proxy request: %s via %s", clientStr.c_str(), sourceIP.toString().c_str());
            return realIP;
        }
        LOG_WARNING("Failed to parse X-Forwarded-For: %s", xff.c_str());
    }
    return sourceIP;
}
