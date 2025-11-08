#include "globals.h"
#include "websocket.h"
#include "file_system.h"
#include "api_system.h"
#include "api_signal_generator.h"
#include "api_gains.h"
#include "api_fir.h"
#include "api_presets.h"
#include "api_preset_config.h"
#include "api_volume.h"
#include "api_helpers.h"
#include "teensy_comm.h"
#include "config.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>

AsyncWebServer server(80);

void handleBackup(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, CONFIG_FILE, "application/msgpack", true);
    response->addHeader("Content-Disposition", "attachment; filename=\"vybes_config.msgpack\"");
    request->send(response);
}

void handleRestoreUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static File restoreFile;
    if (!index) {
        Serial.printf("Restore started: %s\n", filename.c_str());
        restoreFile = LittleFS.open(CONFIG_FILE, "w");
        if (!restoreFile) {
            request->send(500, "text/plain", "Error opening file for writing");
            return;
        }
    }

    if (len) {
        if (restoreFile.write(data, len) != len) {
            request->send(500, "text/plain", "Error writing to file");
            return;
        }
    }

    if (final) {
        if (restoreFile) {
            restoreFile.close();
        }
        Serial.printf("Restore finished: %s, %u B\n", filename.c_str(), index + len);
        request->send(200, "text/plain", "Configuration restored successfully.");
        load_config();
    }
}

void setupWebServer() {
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept, Authorization");

    // API Routes - System Status
    server.on("/status", HTTP_GET, handleGetStatus);
    server.on("/mute/percent", HTTP_PUT, handlePutMutePercent);
    server.on("/mute", HTTP_PUT, handlePutMute);
    server.on("/volume", HTTP_PUT, handlePutVolume);

    // API Routes - Speaker & Input gains
    server.on("/gains/speaker", HTTP_PUT, handlePutSpeakerGain);
    server.addHandler(new AsyncCallbackJsonWebHandler("/gains/input", handlePutInputGains));

    server.on("/preset/gains", HTTP_GET, handleGetPresetGains);
    server.addHandler(new AsyncCallbackJsonWebHandler("/preset/gains", handleSetPresetGains));
    
    // API Routes - FIR Filter Management
    server.on("/fir/files", HTTP_GET, handleGetFirFiles);
    server.on("/preset/fir/enabled", HTTP_PUT, handlePutPresetFirEnabled);
    server.on("/preset/fir", HTTP_PUT, handlePutPresetFir);

    server.on("/preset/active", HTTP_PUT, handlePutActivePreset);

    //Feature enablement
    server.on("/preset/delay/enabled", HTTP_PUT, handlePutPresetDelayEnabled);
    server.on("/preset/eq/enabled", HTTP_PUT, handlePutPresetEQEnabled);
    server.on("/preset/crossover/enabled", HTTP_PUT, handlePutPresetCrossoverEnabled);

    // API Routes - Speaker Configuration
    server.on("/preset/delay", HTTP_PUT, handlePutPresetDelayNamed);

    server.addHandler(new AsyncCallbackJsonWebHandler("/preset/eq", handlePutPresetEQPoints));

    // API Routes - Crossover and Equal Loudness
    server.on("/preset/crossover", HTTP_PUT, handlePutPresetCrossover);

    // API Routes - Preset Management
    server.on("/presets", HTTP_GET, handleGetPresets);
    server.on("/preset", HTTP_DELETE, handleDeletePreset);
    server.on("/preset", HTTP_GET, handleGetPreset);
    
    server.on("/preset", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("action")) {
            String action = request->getParam("action")->value();
            if (action == "create") {
                handlePostPresetCreate(request);
            } else if (action == "copy") {
                handlePostPresetCopy(request);
            }
        }
    });

    server.on("/preset", HTTP_PUT, [](AsyncWebServerRequest *request) {
        if (request->hasParam("action")) {
            String action = request->getParam("action")->value();
            if (action == "rename") {
                handlePutPresetRename(request);
            }
        }
    });

    // API Routes - Backup and Restore
    server.on("/backup", HTTP_GET, handleBackup);
    server.on("/restore", HTTP_POST, [](AsyncWebServerRequest *request) {
        // On request, do nothing. The response will be sent by the upload handler.
    }, handleRestoreUpload);

    // Serve static assets
    server.serveStatic("/assets", LittleFS, "/dist/assets")
        .setCacheControl("public, max-age=31536000, immutable");
    server.serveStatic("/images", LittleFS, "/dist/images");

    // Serve index.html for all other routes
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/dist/index.html", "text/html");
    });

    server.onNotFound([](AsyncWebServerRequest *request){
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404);
        }
    });

    server.begin();
    Serial.println("HTTP server started on port 80");
}