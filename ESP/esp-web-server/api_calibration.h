#ifndef API_CALIBRATION_H
#define API_CALIBRATION_H

#include <ESPAsyncWebServer.h>

void handleGetCalibration(AsyncWebServerRequest *request);
void handlePutCalibrate(AsyncWebServerRequest *request);

#endif // API_CALIBRATION_H
