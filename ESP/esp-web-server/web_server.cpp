#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "file_system.h"
#include "api_system.h"
#include "api_signal_generator.h"
#include "api_gains.h"
#include "api_fir.h"
#include "api_presets.h"
#include "api_preset_config.h"
#include "teensy_comm.h"

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

    String fsPath = "/dist" + path;

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

    String gzPath = fsPath + ".gz";
    if (LittleFS.exists(gzPath)) {
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, gzPath, contentType);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    } else if (LittleFS.exists(fsPath)) {
        request->send(LittleFS, fsPath, contentType);
    } else {
        request->send(404, "text/plain", "Not Found");
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
    server.on("^\\/preset\\/([^\\/]+)\\/eq\\/pref\\/(\\d+)$", HTTP_POST, handlePostPresetEQ);
    server.on("^\\/preset\\/([^\\/]+)\\/eq\\/pref\\/(\\d+)$", HTTP_DELETE, handleDeletePresetEQ);
    server.on("^\\/preset\\/([^\\/]+)\\/eq\\/pref\\/(\\d+)$", HTTP_PUT, [](AsyncWebServerRequest *request){}, NULL, handlePutPresetEQPoints);
    server.on("^\\/preset\\/([^\\/]+)\\/eq\\/pref\\/enabled/(on|off)$", HTTP_PUT, handlePutPresetEQEnabled);

    // API Routes - Crossover and Equal Loudness
    server.on("^\\/preset\\/([^\\/]+)\\/crossover\\/freq\\/(\\d+)$", HTTP_PUT, handlePutPresetCrossover);
    server.on("^\\/preset\\/([^\\/]+)\\/crossover\\/enabled\\/(on|off)$", HTTP_PUT, handlePutPresetCrossoverEnabled);


    // Static file serving
    server.onNotFound(handleFileServing);

    server.begin();
    Serial.println("HTTP server started on port 80");
}
