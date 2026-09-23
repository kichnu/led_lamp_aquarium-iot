#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>

void initWebServer();
bool checkAuthentication(AsyncWebServerRequest* request);

// Owija handler w checkAuthentication() — 401 JSON przy odmowie, inaczej wywołuje fn.
// Użycie: server.on(uri, method, requireAuth(handleXyz));
ArRequestHandlerFunction requireAuth(ArRequestHandlerFunction fn);

#endif
