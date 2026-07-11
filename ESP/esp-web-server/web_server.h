#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <PsychicHttp.h>
#include <PsychicHttpsServer.h>

// Plain HTTP on port 80, HTTPS on 443. Both serve the identical routes; the
// HTTPS listener only starts when certificates exist on LittleFS (see
// docs/WIRING.md and ESP/make-certs.sh).
extern PsychicHttpServer server;
extern PsychicHttpsServer serverHttps;

void setupWebServer();

#endif // WEB_SERVER_H
