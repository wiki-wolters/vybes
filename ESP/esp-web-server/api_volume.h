#ifndef API_VOLUME_H
#define API_VOLUME_H

#include <ESPAsyncWebServer.h>

void handlePutVolume(AsyncWebServerRequest *request);
void increase_volume();
void decrease_volume();
void toggle_mute();

#endif // API_VOLUME_H