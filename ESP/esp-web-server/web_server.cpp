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

void handleRestore(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
        // Open the file for writing
        request->_tempFile = LittleFS.open(CONFIG_FILE, "w");
        if (!request->_tempFile) {
            request->send(500, "text/plain", "Error opening file for writing");
            return;
        }
    }

    if (len) {
        // Write the received chunk to the file
        if (request->_tempFile.write(data, len) != len) {
            request->send(500, "text/plain", "Error writing to file");
            return;
        }
    }

    if (final) {
        // Close the file
        if (request->_tempFile) {
            request->_tempFile.close();
        }
        
        // Send a success response
        request->send(200, "text/plain", "Configuration restored successfully. Restarting device.");

        // Restart the device to apply the new configuration
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
    server.on("^\\/speaker\\/(left|right|sub)\\/gain\\/([\\d.]+)$", HTTP_PUT, handlePutSpeakerGain);
    server.on("^\\/input\\/(bluetooth|spdif)\\/gain\\/([\\d.]+)$", HTTP_PUT, handlePutInputGain);
    
    // API Routes - FIR Filter Management
    server.on("/fir/files", HTTP_GET, handleGetFirFiles);
    server.on("^\\/preset\\/([^\\/]+)\\/fir\\/(left|right|sub)\\/load\\/([^\\/]+)$", HTTP_PUT, handlePutPresetFir);
    server.on("^\\/preset\\/([^\\/]+)\\/fir\\/enabled\\/(on|off)$", HTTP_PUT, handlePutPresetFirEnabled);

    // API Routes - Tone Generation
    server.on("^\\/noise\\/([\\d.]+)$", HTTP_PUT, handlePutNoise);

    // API Routes - Preset Management
    server.on("/presets", HTTP_GET, handleGetPresets);
    server.on("^\\/preset\\/([^\\/]+)$", HTTP_GET, handleGetPreset);
    server.on("^\\/preset\\/create\\/([^\\/]+)$", HTTP_POST, handlePostPresetCreate);
    server.on("^\\/preset\\/copy\\/([^\\/]+)\\/([^\\/]+)$", HTTP_POST, handlePostPresetCopy);
    server.on("^\\/preset\\/rename\\/([^\\/]+)\\/([^\\/]+)$", HTTP_PUT, handlePutPresetRename);
    server.on("^\\/preset\\/([^\\/]+)$", HTTP_DELETE, handleDeletePreset);
    server.on("^\\/preset\\/active\\/([^\\/]+)$", HTTP_PUT, handlePutActivePreset);

    // API Routes - Speaker Configuration
    server.on("^\\/preset\\/([^\\/]+)\\/delay\\/(left|right|sub)\\/([\\d.]+)$", HTTP_PUT, handlePutPresetDelayNamed);
    server.on("^\\/preset\\/([^\\/]+)\\/delay\\/enabled\\/(on|off)$", HTTP_PUT, handlePutPresetDelayEnabled);

    // API Routes - EQ Management
    server.on("^\\/preset\\/([^\\/]+)\\/eq\\/pref\\/0$", HTTP_PUT, [](AsyncWebServerRequest *request){}, NULL, handlePutPresetEQPoints);
    server.on("^\\/preset\\/([^\\/]+)\\/eq\\/pref\\/enabled\\/(on|off)$", HTTP_PUT, handlePutPresetEQEnabled);

    // API Routes - Crossover and Equal Loudness
    server.on("^\\/preset\\/([^\\/]+)\\/crossover\\/freq\\/(\\d+)$", HTTP_PUT, handlePutPresetCrossover);
    server.on("^\\/preset\\/([^\\/]+)\\/crossover\\/enabled\\/(on|off)$", HTTP_PUT, handlePutPresetCrossoverEnabled);

    // API Routes - Backup and Restore
    server.on("/backup", HTTP_GET, handleBackup);
    server.on("/restore", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200);
    }, handleRestore);

    // Serve static assets
    server.serveStatic("/assets", LittleFS, "/dist/assets");

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