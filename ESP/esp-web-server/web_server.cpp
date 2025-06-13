#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "file_system.h"
#include "api_calibration.h"
#include "api_system.h"
#include "api_signal_generator.h"
#include "api_presets.h"
#include "api_preset_config.h"

AsyncWebServer server(80);

void handleFileServing(AsyncWebServerRequest *request) {
    if (request->method() == HTTP_OPTIONS) {
        request->send(200);
        return;
    }
    String path = request->url();
    if (path.endsWith("/")) {
        path += "index.html";
    }
    String contentType = "text/html";
    if (path.endsWith(".css")) {
        contentType = "text/css";
    } else if (path.endsWith(".js")) {
        contentType = "application/javascript";
    } else if (path.endsWith(".png")) {
        contentType = "image/png";
    } else if (path.endsWith(".jpg")) {
        contentType = "image/jpeg";
    } else if (path.endsWith(".ico")) {
        contentType = "image/x-icon";
    }

    if (LittleFS.exists(path)) {
        request->send(LittleFS, path, contentType);
    } else {
        request->send(404, "text/plain", "Not Found");
    }
}

void setupWebServer() {
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept, Authorization");

    // API Routes - Calibration
    server.on("/calibration", HTTP_GET, handleGetCalibration);
    server.on("/calibrate/:spl", HTTP_PUT, handlePutCalibrate);

    // API Routes - System Status
    server.on("/status", HTTP_GET, handleGetStatus);
    server.on("/sub/:command", HTTP_PUT, handlePutSub);
    server.on("/bypass/:state", HTTP_PUT, handlePutBypass);
    server.on("/mute/:state", HTTP_PUT, handlePutMute);
    server.on("/mute/percent/:percent", HTTP_PUT, handlePutMutePercent);

    // API Routes - Tone Generation
    server.on("/generate/tone/:freq/:amp", HTTP_PUT, handlePutTone);
    server.on("/generate/tone/stop", HTTP_PUT, handlePutToneStop);
    server.on("/generate/noise/:volume", HTTP_PUT, handlePutNoise);
    server.on("/pulse", HTTP_PUT, handlePutPulse);

    // API Routes - Preset Management
    server.on("/presets", HTTP_GET, handleGetPresets);
    server.on("/preset/:name", HTTP_GET, handleGetPreset);
    server.on("/preset/create/:name", HTTP_POST, handlePostPresetCreate);
    server.on("/preset/copy/:from/:to", HTTP_POST, handlePostPresetCopy);
    server.on("/preset/rename/:from/:to", HTTP_PUT, handlePutPresetRename);
    server.on("/preset/:name", HTTP_DELETE, handleDeletePreset);
    server.on("/preset/active/:name", HTTP_PUT, handlePutActivePreset);

    // API Routes - Speaker Configuration
    server.on("/preset/delay/:speaker/:delay", HTTP_PUT, handlePutPresetDelay);
    server.on("/preset/:name/delay/:speaker/:delay", HTTP_PUT, handlePutPresetDelayNamed);

    // API Routes - EQ Management
    server.on("/preset/:name/eq/:type/:spl", HTTP_POST, handlePostPresetEQ);
    server.on("/preset/:name/eq/:type/:spl", HTTP_DELETE, handleDeletePresetEQ);

    // API Routes - Crossover and Equal Loudness
    server.on("/preset/:name/crossover/:type/:freq", HTTP_PUT, handlePutPresetCrossover);
    server.on("/preset/:name/equal-loudness/:state", HTTP_PUT, handlePutPresetEqualLoudness);

    // Static file serving
    server.onNotFound(handleFileServing);

    server.begin();
    Serial.println("HTTP server started on port 80");
}
