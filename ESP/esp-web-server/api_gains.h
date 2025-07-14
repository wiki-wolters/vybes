#ifndef API_SPEAKER_H
#define API_SPEAKER_H

#include <ESPAsyncWebServer.h>

void handlePutSpeakerGain(AsyncWebServerRequest *request);
void handlePutInputGains(AsyncWebServerRequest *request, JsonVariant &json);

#endif // API_SPEAKER_H
