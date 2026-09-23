
#include "web_server.h"
#include "web_handlers.h"
#include "../security/session_manager.h"
#include "../security/rate_limiter.h"
#include "../security/auth_manager.h"
#include "../config/credentials_manager.h"
#include "../core/logging.h"

AsyncWebServer server(80);

ArRequestHandlerFunction requireAuth(ArRequestHandlerFunction fn) {
    return [fn](AsyncWebServerRequest* request) {
        if (!checkAuthentication(request)) {
            request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
            return;
        }
        fn(request);
    };
}

void initWebServer() {
    // Static pages
    server.on("/", HTTP_GET, handleDashboard);
    server.on("/login", HTTP_GET, handleLoginPage);

    // Authentication
    server.on("/api/login", HTTP_POST, handleLogin);
    server.on("/api/logout", HTTP_POST, handleLogout);

    // Health check endpoint (no session required)
    server.on("/api/health", HTTP_GET, handleHealth);

    // Thermo Control API endpoints
    registerThermoHandlers(server);

    // 404 handler - also enforce whitelist
    server.onNotFound([](AsyncWebServerRequest* request) {
        IPAddress clientIP = request->client()->remoteIP();
        if (!isTrustedProxy(clientIP) && !isIPAllowed(clientIP)) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }
        request->send(404, "text/plain", "Not Found");
    });
    
    server.begin();
    LOG_INFO("");
    LOG_INFO("Web server started on port 80");
}

bool checkAuthentication(AsyncWebServerRequest* request) {
    IPAddress clientIP = request->client()->remoteIP();
    
    // ============== TRUSTED PROXY CHECK ==============
    // WireGuard cryptographically authenticates the VPS — IP check is sufficient
    if (isTrustedProxy(clientIP)) {
        return true;
    }
    
    // ============== WHITELIST CHECK ==============
    // Only whitelisted IPs can access the system
    if (!isIPAllowed(clientIP)) {
        LOG_WARNING("IP %s not on whitelist - access denied", clientIP.toString().c_str());
        return false;
    }

    // Check if IP is blocked
    if (isIPBlocked(clientIP)) {
        return false;
    }
    
    // Check rate limiting
    if (isRateLimited(clientIP)) {
        recordFailedAttempt(clientIP);
        return false;
    }
    
    recordRequest(clientIP);
    
    // Check session cookie
    if (request->hasHeader("Cookie")) {
        String cookie = request->getHeader("Cookie")->value();
        int tokenStart = cookie.indexOf("session_token=");
        if (tokenStart != -1) {
            tokenStart += 14;
            int tokenEnd = cookie.indexOf(";", tokenStart);
            if (tokenEnd == -1) tokenEnd = cookie.length();
            
            String token = cookie.substring(tokenStart, tokenEnd);
            if (validateSession(token, clientIP)) {
                return true;
            }
        }
    }
    
    recordFailedAttempt(clientIP);
    return false;
}