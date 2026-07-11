#ifndef API_VOLUME_H
#define API_VOLUME_H

#include <PsychicHttp.h>

esp_err_t handlePutVolume(PsychicRequest *request);
void increase_volume(int amount = 2);
void decrease_volume(int amount = 2);
void toggle_mute();

#endif // API_VOLUME_H
