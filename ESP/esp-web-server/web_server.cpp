#include "globals.h"
#include "websocket.h"
#include "file_system.h"
#include "api_system.h"
#include "api_signal_generator.h"
#include "api_gains.h"
#include "api_fir.h"
#include "api_presets.h"
#include "api_preset_config.h"
#include "teensy_comm.h"
#include <ESPAsyncWebServer.h>
#include "config.h"

AsyncWebServer server(80);



void handleBackup(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, CONFIG_FILE, "application/json", true);
    response->addHeader("Content-Disposition", "attachment; filename=\"vybes_config.json\"");
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
        request->send(200, "text/plain", "Configuration restored successfully. Restarting device.");
        ESP.restart();
    }
}

void setupWebServer() {
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept, Authorization");

    // API Routes - System Status
    server.on("/status", HTTP_GET, handleGetStatus);
    server.on("^\\/mute\\/(on|off)$", HTTP_PUT, handlePutMute);
    server.on("^\\/mute\\/percent\\/(\\d+)$", HTTP_PUT, handlePutMutePercent);

    // API Routes - Speaker & Input gains
    server.on("^\/speaker\/(left|right|sub)\/gain\/([\d.]+)$", HTTP_PUT, handlePutSpeakerGain);
    server.on("/gains/input", HTTP_PUT, [](AsyncWebServerRequest *request){}, NULL, handlePutInputGains);
    
    // API Routes - FIR Filter Management
    server.on("/fir/files", HTTP_GET, handleGetFirFiles);
    server.on("^\\/preset\\/([^\\/]+)\\/fir\\/(left|right|sub)\\/load\\/([^\\/]+)$", HTTP_PUT, handlePutPresetFir);
    server.on("^\\/preset\\/([^\\/]+)\\/fir\\/enabled\\/(on|off)$", HTTP_PUT, handlePutPresetFirEnabled);

    // API Routes - Tone Generation
    server.on("^\\/noise\\/([\\d.]+)$", HTTP_PUT, handlePutNoise);

    // API Routes - Preset Management
    server.on("/presets", HTTP_GET, handleGetPresets);
    server.on("/preset/*", HTTP_DELETE, handleDeletePreset);
    server.on("/preset/*", HTTP_GET, handleGetPreset);
    server.on("/preset/create/*", HTTP_POST, handlePostPresetCreate);

    server.on("^\\/preset\\/(copy|rename)\\/([^\\/]+)\\/([^\\/]+)$", HTTP_POST|HTTP_PUT, [](AsyncWebServerRequest *request){
        if (request->method() == HTTP_POST) {
            handlePostPresetCopy(request);
        } else if (request->method() == HTTP_PUT) {
            handlePutPresetRename(request);
        }
    });
    server.on("/preset/active/*", HTTP_PUT, handlePutActivePreset);

    //Feature enablement
    server.on("^\\/preset\\/([^\\/]+)\\/(delay|eq\\/pref|crossover)\\/enabled\\/(on|off)$", HTTP_PUT, [](AsyncWebServerRequest *request){
        String feature = request->pathArg(1);
        Serial.print("Toggle preset feature: "); Serial.println(feature);

        if (feature == "delay") {
            handlePutPresetDelayEnabled(request);
        } else if (feature == "eq/pref") {
            handlePutPresetEQEnabled(request);
        } else if (feature == "crossover") {
            handlePutPresetCrossoverEnabled(request);
        }
    });

    // API Routes - Speaker Configuration
    server.on("^\\/preset\\/([^\\/]+)\\/delay\\/(left|right|sub)\\/([\\d.]+)$", HTTP_PUT, handlePutPresetDelayNamed);

    // API Routes - EQ Management
    server.on("^\\/preset\\/([^\\/]+)\\/eq\\/pref\\/0$", HTTP_PUT, [](AsyncWebServerRequest *request){}, NULL, handlePutPresetEQPoints);

    // API Routes - Crossover and Equal Loudness
    server.on("^\\/preset\\/([^\\/]+)\\/crossover\\/freq\\/(\\d+)$", HTTP_PUT, handlePutPresetCrossover);

    // API Routes - Backup and Restore
    server.on("/backup", HTTP_GET, handleBackup);
    server.on("/restore", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200);
    }, handleRestoreUpload);

    // Serve static assets
    server.serveStatic("/assets", LittleFS, "/dist/assets");
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