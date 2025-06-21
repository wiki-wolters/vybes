#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "file_system.h"
#include "api_system.h"
#include "api_signal_generator.h"
#include "api_speaker.h"
#include "api_fir.h"
#include "api_presets.h"
#include "api_preset_config.h"
#include "api_delay.h"

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
    server.on("/mute/:state", HTTP_PUT, handlePutMute);
    server.on("/mute/percent/:percent", HTTP_PUT, handlePutMutePercent);

    // API Routes - Speaker Controls
    server.on("/speaker/:speaker/gain/:gain", HTTP_PUT, handlePutSpeakerGain);
    server.on("/input/:input/gain/:gain", HTTP_PUT, handlePutInputGain);
    
    // API Routes - FIR Filter Management
    server.on("/fir/files", HTTP_GET, handleGetFirFiles);
    server.on("/preset/:name/fir/:speaker/load/:filename", HTTP_PUT, handlePutPresetFir);
    server.on("/preset/:name/fir/enabled/:state", HTTP_PUT, handlePutPresetFirEnabled);

    // API Routes - Tone Generation
    server.on("/noise/:volume", HTTP_PUT, handlePutNoise);

    // API Routes - Preset Management
    server.on("/presets", HTTP_GET, handleGetPresets);
    server.on("/preset/:name", HTTP_GET, handleGetPreset);
    server.on("/preset/create/:name", HTTP_POST, handlePostPresetCreate);
    server.on("/preset/copy/:from/:to", HTTP_POST, handlePostPresetCopy);
    server.on("/preset/rename/:from/:to", HTTP_PUT, handlePutPresetRename);
    server.on("/preset/:name", HTTP_DELETE, handleDeletePreset);
    server.on("/preset/active/:name", HTTP_PUT, handlePutActivePreset);

    // API Routes - Speaker Configuration
    server.on("/preset/:name/delay/:speaker/:delay", HTTP_PUT, handlePutPresetDelay);
    server.on("/preset/:name/delay/enabled/:state", HTTP_PUT, handlePutPresetDelayEnabled);
    server.on("/preset/:name/delays", HTTP_GET, handleGetDelays);
    
    // API Routes - Input Gains
    server.on("/input/gains", HTTP_PUT, [](AsyncWebServerRequest *request) {
        if (request->hasParam("left", true) && request->hasParam("right", true)) {
            float leftGain = request->getParam("left", true)->value().toFloat();
            float rightGain = request->getParam("right", true)->value().toFloat();
            
            // Update config
            current_config.inputLeftGain = leftGain;
            current_config.inputRightGain = rightGain;
            scheduleConfigWrite();
            
            // Send to Teensy
            sendToTeensy(CMD_SET_INPUT_GAINS, 
                        String(leftGain, 2), 
                        String(rightGain, 2));
            
            // Prepare response
            DynamicJsonDocument doc(128);
            doc["status"] = "ok";
            doc["leftGain"] = leftGain;
            doc["rightGain"] = rightGain;
            
            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
            
            // Broadcast update
            broadcastWebSocket(response);
        } else {
            request->send(400, "text/plain", "Missing left or right gain parameter");
        }
    });

    // API Routes - EQ Management
    server.on("/preset/:name/eq/:type/:spl", HTTP_POST, handlePostPresetEQ);
    server.on("/preset/:name/eq/:type/:spl", HTTP_DELETE, handleDeletePresetEQ);
    server.on("/preset/:name/eq/:type/:spl", HTTP_PUT, [](AsyncWebServerRequest *request){}, NULL, handlePutPresetEQPoints);
    server.on("/preset/:name/eq/:type/enabled/:state", HTTP_PUT, handlePutPresetEQEnabled);

    // API Routes - Crossover and Equal Loudness
    server.on("/preset/:name/crossover/:type/:freq", HTTP_PUT, handlePutPresetCrossover);
    server.on("/preset/:name/crossover/enabled/:state", HTTP_PUT, handlePutPresetCrossoverEnabled);


    // Static file serving
    server.onNotFound(handleFileServing);

    server.begin();
    Serial.println("HTTP server started on port 80");
}
