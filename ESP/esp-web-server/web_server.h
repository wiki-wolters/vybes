#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <PsychicHttp.h>
#include <PsychicHttpsServer.h>

// Plain HTTP on port 80, HTTPS on 443. Both serve the identical routes. The
// HTTPS listener is only built on the ESP32-S3 (the classic ESP32 lacks the
// RAM for TLS) and only starts when certificates exist on LittleFS (see
// docs/WIRING.md and ESP/make-certs.sh).
extern PsychicHttpServer server;
#ifdef CONFIG_IDF_TARGET_ESP32S3
extern PsychicHttpsServer serverHttps;
#endif

void setupWebServer();

#endif // WEB_SERVER_H
