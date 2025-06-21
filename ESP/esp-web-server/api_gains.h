#ifndef API_SPEAKER_H
#define API_SPEAKER_H

#include <ESPAsyncWebServer.h>

void handlePutSpeakerGain(AsyncWebServerRequest *request);
void handlePutInputGain(AsyncWebServerRequest *request);

#endif // API_SPEAKER_H
