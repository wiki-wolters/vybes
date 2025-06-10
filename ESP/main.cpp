#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <uri/UriRegex.h>

// Server and WebSocket setup
ESP8266WebServer server(80);
WebSocketsServer webSocket(8080);
WiFiManager wifiManager;

// Debounced write system
unsigned long lastConfigChange = 0;
const unsigned long WRITE_DELAY = 500; // 500ms delay before writing to flash
bool configChanged = false;

// System settings structure
struct SystemSettings {
    int calibrationSpl = 85;
    bool isCalibrated = false;
    String subwooferState = "on";
    String bypassState = "off";
    String muteState = "off";
    int mutePercent = 0;
    int toneFrequency = 1000;
    int toneVolume = 50;
    int noiseVolume = 0;
    String currentPreset = "";
};

SystemSettings systemSettings;

// Function prototypes
void setupWiFi();
void setupWebServer();
void setupWebSocket();
void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
void broadcastWebSocket(const String message);
void loadSystemSettings();
void saveSystemSettings();
bool savePreset(const String presetName, const String presetData);
bool deletePreset(const String presetName);
String getPreset(const String presetName);
void handleDebounceWrite();
void sendToTeensy(const String command, const String data);
String urlDecode(String str);

// Web server handlers
void handleGetCalibration();
void handlePutCalibrate();
void handleGetStatus();
void handlePutSub();
void handlePutBypass();
void handlePutMute();
void handlePutMutePercent();
void handlePutTone();
void handlePutToneStop();
void handlePutNoise();
void handlePutPulse();
void handleGetPresets();
void handleGetPreset();
void handlePostPresetCreate();
void handlePostPresetCopy();
void handlePutPresetRename();
void handleDeletePreset();
void handlePutPresetActive();
void handlePutSpeakerDelay();
void handlePutSpeakerDelayNamed();
void handlePostEQ();
void handleDeleteEQ();
void handlePutCrossover();
void handlePutEqualLoudness();
void handleNotFound();
void handleFileServing();
void handlePutActivePreset();
void handlePutPresetCrossover();
void handlePutPresetEqualLoudness();
void handleDeletePresetEQ();
void handlePostPresetEQ();
void handlePutPresetDelayNamed();
void handlePutPresetDelay();

void setup() {
    Serial.begin(115200);
    Serial.println("Vybes DSP Starting...");

    // Initialize I2C for Teensy communication
    Wire.begin();

    // Initialize LittleFS
    if (!LittleFS.begin()) {
        Serial.println("LittleFS mount failed, formatting...");
        LittleFS.format();
        if (!LittleFS.begin()) {
            Serial.println("LittleFS format failed!");
            ESP.restart();
        }
    }

    // Load configuration
    loadSystemSettings();

    // Setup WiFi
    setupWiFi();

    // Setup mDNS
    if (MDNS.begin("vybes")) {
        Serial.println("mDNS responder started: vybes.local");
        MDNS.addService("http", "tcp", 80);
    }

    // Setup web server and WebSocket
    setupWebServer();
    setupWebSocket();

    Serial.println("Vybes DSP ready!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void loop() {
    server.handleClient();
    webSocket.loop();
    MDNS.update();

    // Handle debounced config writes
    handleDebounceWrite();

    // TODO: Handle any incoming I2C communication from Teensy
}

void setupWiFi() {
    // Set custom AP name and portal timeout
    wifiManager.setAPCallback([](WiFiManager * myWiFiManager) {
        Serial.println("Entered config mode");
        Serial.println("AP Name: Vybes-Config");
        Serial.println("IP: 192.168.4.1");
    });

    wifiManager.setConfigPortalTimeout(300); // 5 minute timeout

    // Try to connect; if it fails, start config portal
    if (!wifiManager.autoConnect("Vybes-Config")) {
        Serial.println("Failed to connect and hit timeout");
        ESP.restart();
    }

    Serial.println("WiFi connected!");
}

void setupWebServer() {
    // Enable CORS for all routes
    server.enableCORS(true);

    // API Routes - Calibration
    server.on("/calibration", HTTP_GET, handleGetCalibration);
    server.on(UriRegex("/calibrate/(.+)"), HTTP_PUT, handlePutCalibrate);

    // API Routes - System Status
    server.on("/status", HTTP_GET, handleGetStatus);
    server.on(UriRegex("/sub/(.+)"), HTTP_PUT, handlePutSub);
    server.on(UriRegex("/bypass/(.+)"), HTTP_PUT, handlePutBypass);
    server.on(UriRegex("/mute/(.+)"), HTTP_PUT, handlePutMute);
    server.on(UriRegex("/mute/percent/(.+)"), HTTP_PUT, handlePutMutePercent);

    // API Routes - Tone Generation
    server.on(UriRegex("/generate/tone/(.+)/(.+)"), HTTP_PUT, handlePutTone);
    server.on("/generate/tone/stop", HTTP_PUT, handlePutToneStop);
    server.on(UriRegex("/generate/noise/(.+)"), HTTP_PUT, handlePutNoise);
    server.on("/pulse", HTTP_PUT, handlePutPulse);

    // API Routes - Preset Management
    server.on("/presets", HTTP_GET, handleGetPresets);
    server.on(UriRegex("/preset/(.+)"), HTTP_GET, handleGetPreset);
    server.on(UriRegex("/preset/create/(.+)"), HTTP_POST, handlePostPresetCreate);
    server.on(UriRegex("/preset/copy/(.+)/(.+)"), HTTP_POST, handlePostPresetCopy);
    server.on(UriRegex("/preset/rename/(.+)/(.+)"), HTTP_PUT, handlePutPresetRename);
    server.on(UriRegex("/preset/(.+)"), HTTP_DELETE, handleDeletePreset);
    server.on(UriRegex("/preset/active/(.+)"), HTTP_PUT, handlePutActivePreset);

    // API Routes - Speaker Configuration
    server.on(UriRegex("/preset/delay/(.+)/(.+)"), HTTP_PUT, handlePutPresetDelay);
    server.on(UriRegex("/preset/(.+)/delay/(.+)/(.+)"), HTTP_PUT, handlePutPresetDelayNamed);

    // API Routes - EQ Management
    server.on(UriRegex("/preset/(.+)/eq/(.+)/(.+)"), HTTP_POST, handlePostPresetEQ);
    server.on(UriRegex("/preset/(.+)/eq/(.+)/(.+)"), HTTP_DELETE, handleDeletePresetEQ);

    // API Routes - Crossover and Equal Loudness
    server.on(UriRegex("/preset/(.+)/crossover/(.+)/(.+)"), HTTP_PUT, handlePutPresetCrossover);
    server.on(UriRegex("/preset/(.+)/equal-loudness/(.+)"), HTTP_PUT, handlePutPresetEqualLoudness);

    // Static file serving
    server.onNotFound(handleFileServing);

    server.begin();
    Serial.println("HTTP server started on port 80");
}


void setupWebSocket() {
    webSocket.begin();
    webSocket.onEvent(handleWebSocketEvent);
    Serial.println("WebSocket server started on port 8080");
}

void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
    case WStype_DISCONNECTED:
        Serial.printf("WebSocket [%u] Disconnected\n", num);
        break;
    case WStype_CONNECTED:
        {
            IPAddress ip = webSocket.remoteIP(num);
            Serial.printf("WebSocket [%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
        }
        break;
    case WStype_TEXT:
        Serial.printf("WebSocket [%u] Received: %s\n", num, payload);
        break;
    default:
        break;
    }
}

void broadcastWebSocket(String message) {
    webSocket.broadcastTXT(message);
    Serial.println("WebSocket broadcast: " + message);
}

void loadSystemSettings() {
    if (LittleFS.exists("/system.json")) {
        File file = LittleFS.open("/system.json", "r");
        if (file) {
            DynamicJsonDocument doc(1024);
            DeserializationError error = deserializeJson(doc, file);
            file.close();

            if (!error) {
                systemSettings.calibrationSpl = doc["calibrationSpl"] | 85;
                systemSettings.isCalibrated = doc["isCalibrated"] | false;
                systemSettings.subwooferState = doc["subwooferState"] | "on";
                systemSettings.bypassState = doc["bypassState"] | "off";
                systemSettings.muteState = doc["muteState"] | "off";
                systemSettings.mutePercent = doc["mutePercent"] | 0;
                systemSettings.toneFrequency = doc["toneFrequency"] | 1000;
                systemSettings.toneVolume = doc["toneVolume"] | 50;
                systemSettings.noiseVolume = doc["noiseVolume"] | 0;
                systemSettings.currentPreset = doc["currentPreset"] | "";
                Serial.println("System settings loaded");
                return;
            }
        }
    }
    Serial.println("System settings not found, using defaults");
}

void saveSystemSettings() {
    DynamicJsonDocument doc(1024);
    doc["calibrationSpl"] = systemSettings.calibrationSpl;
    doc["isCalibrated"] = systemSettings.isCalibrated;
    doc["subwooferState"] = systemSettings.subwooferState;
    doc["bypassState"] = systemSettings.bypassState;
    doc["muteState"] = systemSettings.muteState;
    doc["mutePercent"] = systemSettings.mutePercent;
    doc["toneFrequency"] = systemSettings.toneFrequency;
    doc["toneVolume"] = systemSettings.toneVolume;
    doc["noiseVolume"] = systemSettings.noiseVolume;
    doc["currentPreset"] = systemSettings.currentPreset;

    File file = LittleFS.open("/system.json", "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
        Serial.println("System settings saved");
    } else {
        Serial.println("Failed to save system settings");
    }
}

void scheduleConfigWrite() {
    configChanged = true;
    lastConfigChange = millis();
}

bool loadPresetFromFile(String filename, DynamicJsonDocument & doc) {
    if (LittleFS.exists(filename)) {
        File file = LittleFS.open(filename, "r");
        if (file) {
            DeserializationError error = deserializeJson(doc, file);
            file.close();
            return !error;
        }
    }
    return false;
}

bool savePresetToFile(String filename, DynamicJsonDocument & doc) {
    File file = LittleFS.open(filename, "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
        return true;
    }
    return false;
}


bool savePreset(const String presetName,
    const String presetData) {
    String filename = "/preset_" + presetName + ".json";
    File file = LittleFS.open(filename, "w");
    if (file) {
        file.print(presetData);
        file.close();
        Serial.println("Preset saved: " + presetName);
        return true;
    }
    Serial.println("Failed to save preset: " + presetName);
    return false;
}

bool deletePreset(const String presetName) {
    String filename = "/preset_" + presetName + ".json";
    if (LittleFS.exists(filename)) {
        LittleFS.remove(filename);
        Serial.println("Preset deleted: " + presetName);
        return true;
    }
    return false;
}

String getPreset(const String presetName) {
    String filename = "/preset_" + presetName + ".json";
    if (LittleFS.exists(filename)) {
        File file = LittleFS.open(filename, "r");
        if (file) {
            String content = file.readString();
            file.close();
            return content;
        }
    }
    return "";
}

void handleDebounceWrite() {
    if (configChanged && (millis() - lastConfigChange > WRITE_DELAY)) {
        saveSystemSettings();
        configChanged = false;
    }
}

void sendToTeensy(const String command,
    const String data) {
    // TODO: Implement I2C communication to Teensy
    // Wire.beginTransmission(TEENSY_I2C_ADDRESS);
    // Wire.write(command.c_str());
    // Wire.write(data.c_str());
    // Wire.endTransmission();
    Serial.println("TODO: Send to Teensy - " + command + ": " + data);
}

// API Handler Implementations

void handleGetCalibration() {
    DynamicJsonDocument doc(256);
    doc["isCalibrated"] = systemSettings.isCalibrated;
    if (systemSettings.isCalibrated) {
        doc["spl"] = systemSettings.calibrationSpl;
    }

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handlePutCalibrate() {
    String splStr = server.pathArg(0);
    int spl = splStr.toInt();

    if (spl < 40 || spl > 120) {
        server.send(400, "application/json", "{\"error\":\"SPL must be between 40 and 120\"}");
        return;
    }

    systemSettings.calibrationSpl = spl;
    systemSettings.isCalibrated = true;
    lastConfigChange = millis();
    configChanged = true;

    // Broadcast WebSocket update
    DynamicJsonDocument wsDoc(256);
    wsDoc["event"] = "calibration";
    wsDoc["spl"] = spl;
    String wsMessage;
    serializeJson(wsDoc, wsMessage);
    broadcastWebSocket(wsMessage);

    // TODO: Send calibration to Teensy
    sendToTeensy("CALIBRATE", String(spl));

    server.send(200, "application/json", "{\"success\":true,\"spl\":" + String(spl) + "}");
}

void handleGetStatus() {
    DynamicJsonDocument doc(1024);

    doc["calibration"]["isCalibrated"] = systemSettings.isCalibrated;
    if (systemSettings.isCalibrated) {
        doc["calibration"]["spl"] = systemSettings.calibrationSpl;
    }

    doc["subwoofer"] = systemSettings.subwooferState;
    doc["bypass"] = systemSettings.bypassState;
    doc["mute"]["state"] = systemSettings.muteState;
    doc["mute"]["percent"] = systemSettings.mutePercent;
    doc["tone"]["frequency"] = systemSettings.toneFrequency;
    doc["tone"]["volume"] = systemSettings.toneVolume;
    doc["noise"]["volume"] = systemSettings.noiseVolume;
    doc["currentPreset"] = systemSettings.currentPreset;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}


void handlePutSub() {
    String state = server.pathArg(0);
    if (state != "on" && state != "off") {
        server.send(400, "application/json", "{\"error\":\"State must be 'on' or 'off'\"}");
        return;
    }

    systemSettings.subwooferState = state;
    lastConfigChange = millis();
    configChanged = true;

    // Broadcast WebSocket update
    DynamicJsonDocument wsDoc(256);
    wsDoc["event"] = "subwoofer";
    wsDoc["state"] = state;
    String wsMessage;
    serializeJson(wsDoc, wsMessage);
    broadcastWebSocket(wsMessage);

    // TODO: Send subwoofer state to Teensy
    sendToTeensy("SUBWOOFER", state);

    server.send(200, "application/json", "{\"success\":true,\"state\":\"" + state + "\"}");
}

void handlePutBypass() {
    String state = server.pathArg(0);
    if (state != "on" && state != "off") {
        server.send(400, "application/json", "{\"error\":\"State must be 'on' or 'off'\"}");
        return;
    }

    systemSettings.bypassState = state;
    lastConfigChange = millis();
    configChanged = true;

    // Broadcast WebSocket update
    DynamicJsonDocument wsDoc(256);
    wsDoc["event"] = "bypass";
    wsDoc["state"] = state;
    String wsMessage;
    serializeJson(wsDoc, wsMessage);
    broadcastWebSocket(wsMessage);

    // TODO: Send bypass state to Teensy
    sendToTeensy("BYPASS", state);

    server.send(200, "application/json", "{\"success\":true,\"state\":\"" + state + "\"}");
}

void handlePutMute() {
    String state = server.pathArg(0);
    if (state != "on" && state != "off") {
        server.send(400, "application/json", "{\"error\":\"State must be 'on' or 'off'\"}");
        return;
    }

    systemSettings.muteState = state;
    lastConfigChange = millis();
    configChanged = true;

    // Broadcast WebSocket update
    DynamicJsonDocument wsDoc(256);
    wsDoc["event"] = "mute";
    wsDoc["state"] = state;
    String wsMessage;
    serializeJson(wsDoc, wsMessage);
    broadcastWebSocket(wsMessage);

    // TODO: Send mute state to Teensy
    sendToTeensy("MUTE", state);

    server.send(200, "application/json", "{\"success\":true,\"state\":\"" + state + "\"}");
}

void handlePutMutePercent() {
    String percentStr = server.pathArg(0);
    int percent = percentStr.toInt();

    if (percent < 1 || percent > 100) {
        server.send(400, "application/json", "{\"error\":\"Percent must be between 1 and 100\"}");
        return;
    }

    systemSettings.mutePercent = percent;
    lastConfigChange = millis();
    configChanged = true;

    // Broadcast WebSocket update
    DynamicJsonDocument wsDoc(256);
    wsDoc["event"] = "mute_percent";
    wsDoc["percent"] = percent;
    String wsMessage;
    serializeJson(wsDoc, wsMessage);
    broadcastWebSocket(wsMessage);

    // TODO: Send mute percent to Teensy
    sendToTeensy("MUTE_PERCENT", String(percent));

    server.send(200, "application/json", "{\"success\":true,\"percent\":" + String(percent) + "}");
}

void handlePutTone() {
    String freqStr = server.pathArg(0);
    String volumeStr = server.pathArg(1);
    int freq = freqStr.toInt();
    int volume = volumeStr.toInt();

    if (freq < 10 || freq > 20000) {
        server.send(400, "application/json", "{\"error\":\"Frequency must be between 10 and 20000 Hz\"}");
        return;
    }
    if (volume < 1 || volume > 100) {
        server.send(400, "application/json", "{\"error\":\"Volume must be between 1 and 100\"}");
        return;
    }

    systemSettings.toneFrequency = freq;
    systemSettings.toneVolume = volume;
    lastConfigChange = millis();
    configChanged = true;

    // Broadcast WebSocket update
    DynamicJsonDocument wsDoc(256);
    wsDoc["event"] = "tone";
    wsDoc["frequency"] = freq;
    wsDoc["volume"] = volume;
    String wsMessage;
    serializeJson(wsDoc, wsMessage);
    broadcastWebSocket(wsMessage);

    // TODO: Send tone generation to Teensy
    sendToTeensy("TONE", String(freq) + "," + String(volume));

    server.send(200, "application/json", "{\"success\":true,\"frequency\":" + String(freq) + ",\"volume\":" + String(volume) + "}");
}

void handlePutToneStop() {
    systemSettings.toneFrequency = 0;
    systemSettings.toneVolume = 0;
    scheduleConfigWrite();
    // Broadcast WebSocket update
    DynamicJsonDocument wsDoc(256);
    wsDoc["event"] = "tone";
    wsDoc["frequency"] = 0;
    wsDoc["volume"] = 0;
    wsDoc["stopped"] = true;
    String wsMessage;
    serializeJson(wsDoc, wsMessage);
    broadcastWebSocket(wsMessage);

    // TODO: Send tone stop to Teensy
    sendToTeensy("TONE_STOP", "");

    server.send(200, "application/json", "{\"success\":true,\"message\":\"Tone generation stopped\"}");
}

void handlePutNoise() {
    String volumeStr = server.pathArg(0);
    int volume = volumeStr.toInt();

    if (volume < 0 || volume > 100) {
        server.send(400, "application/json", "{\"error\":\"Volume must be between 0 and 100\"}");
        return;
    }

    systemSettings.noiseVolume = volume;
    lastConfigChange = millis();
    configChanged = true;

    // Broadcast WebSocket update
    DynamicJsonDocument wsDoc(256);
    wsDoc["event"] = "noise";
    wsDoc["volume"] = volume;
    String wsMessage;
    serializeJson(wsDoc, wsMessage);
    broadcastWebSocket(wsMessage);

    // TODO: Send noise generation to Teensy
    sendToTeensy("NOISE", String(volume));

    server.send(200, "application/json", "{\"success\":true,\"volume\":" + String(volume) + "}");
}

void handlePutPulse() {
    // Broadcast WebSocket update
    DynamicJsonDocument wsDoc(256);
    wsDoc["event"] = "pulse";
    wsDoc["timestamp"] = millis();
    String wsMessage;
    serializeJson(wsDoc, wsMessage);
    broadcastWebSocket(wsMessage);

    // TODO: Send pulse command to Teensy
    sendToTeensy("PULSE", "");

    server.send(200, "application/json", "{\"success\":true,\"message\":\"Playing test pulse\"}");
}

void handleGetPresets() {
    DynamicJsonDocument doc(2048);
    JsonArray presets = doc.to < JsonArray > ();

    Dir dir = LittleFS.openDir("/");
    while (dir.next()) {
        String filename = dir.fileName();
        if (filename.startsWith("/preset_") && filename.endsWith(".json")) {
            String presetName = filename.substring(8, filename.length() - 5); // Remove "/preset_" and ".json"

            JsonObject preset = presets.createNestedObject();
            preset["name"] = presetName;
            preset["isCurrent"] = (presetName == systemSettings.currentPreset);
        }
    }

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleGetPreset() {
    String presetName = server.pathArg(0);
    String presetData = getPreset(presetName);

    if (presetData.isEmpty()) {
        server.send(404, "application/json", "{\"error\":\"Preset not found\"}");
        return;
    }

    // Parse and add isCurrent field
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, presetData);

    if (error) {
        server.send(500, "application/json", "{\"error\":\"Corrupted preset data\"}");
        return;
    }

    doc["isCurrent"] = (presetName == systemSettings.currentPreset);

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handlePostPresetCreate() {
    String presetName = server.pathArg(0);

    // Check if preset already exists
    if (!getPreset(presetName).isEmpty()) {
        server.send(400, "application/json", "{\"error\":\"Preset already exists\"}");
        return;
    }

    // Create default preset structure
    DynamicJsonDocument doc(2048);
    doc["name"] = presetName;
    doc["speakerDelays"]["left"] = 0;
    doc["speakerDelays"]["right"] = 0;
    doc["speakerDelays"]["sub"] = 0;
    doc["crossover"]["frequency"] = 80;
    doc["crossover"]["slope"] = "12";
    doc["equalLoudness"] = false;

    // Create default PEQ points
    JsonArray roomCorrection = doc.createNestedArray("roomCorrection");
    JsonObject roomEQ = roomCorrection.createNestedObject();
    roomEQ["spl"] = 0;
    JsonArray roomPeqSet = roomEQ.createNestedArray("peqSet");

    JsonArray preferenceEQ = doc.createNestedArray("preferenceEQ");
    JsonObject prefEQ = preferenceEQ.createNestedObject();
    prefEQ["spl"] = 0;
    JsonArray prefPeqSet = prefEQ.createNestedArray("peqSet");

    // Add default PEQ points
    for (int freq: {
            100, 1000, 10000
        }) {
        JsonObject roomPoint = roomPeqSet.createNestedObject();
        roomPoint["frequency"] = freq;
        roomPoint["gain"] = 0;
        roomPoint["q"] = 1.0;
        roomPoint["enabled"] = true;

        JsonObject prefPoint = prefPeqSet.createNestedObject();
        prefPoint["frequency"] = freq;
        prefPoint["gain"] = 0;
        prefPoint["q"] = 1.0;
        prefPoint["enabled"] = true;
    }

    String presetData;
    serializeJson(doc, presetData);

    if (savePreset(presetName, presetData)) {
        // Set as current preset
        systemSettings.currentPreset = presetName;
        lastConfigChange = millis();
        configChanged = true;

        // Broadcast WebSocket update
        DynamicJsonDocument wsDoc(256);
        wsDoc["event"] = "preset";
        wsDoc["action"] = "created";
        wsDoc["name"] = presetName;
        String wsMessage;
        serializeJson(wsDoc, wsMessage);
        broadcastWebSocket(wsMessage);

        // Add isCurrent field for response
        doc["isCurrent"] = true;
        String response;
        serializeJson(doc, response);
        server.send(201, "application/json", response);
    } else {
        server.send(500, "application/json", "{\"error\":\"Failed to create preset\"}");
    }
}

void handlePostPresetCopy() {
    String sourceName = server.pathArg(0);
    String newName = server.pathArg(1);

    String sourceData = getPreset(sourceName);
    if (sourceData.isEmpty()) {
        server.send(404, "application/json", "{\"error\":\"Source preset not found\"}");
        return;
    }

    // Check if new preset name already exists
    if (!getPreset(newName).isEmpty()) {
        server.send(400, "application/json", "{\"error\":\"Preset already exists\"}");
        return;
    }

    // Parse source data and change name
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, sourceData);

    if (error) {
        server.send(500, "application/json", "{\"error\":\"Corrupted source preset data\"}");
        return;
    }

    doc["name"] = newName;

    String newPresetData;
    serializeJson(doc, newPresetData);

    if (savePreset(newName, newPresetData)) {
        // Broadcast WebSocket update
        DynamicJsonDocument wsDoc(256);
        wsDoc["event"] = "preset";
        wsDoc["action"] = "copied";
        wsDoc["source"] = sourceName;
        wsDoc["name"] = newName;
        String wsMessage;
        serializeJson(wsDoc, wsMessage);
        broadcastWebSocket(wsMessage);

        server.send(200, "application/json", "{\"success\":true,\"name\":\"" + newName + "\"}");
    } else {
        server.send(500, "application/json", "{\"error\":\"Failed to copy preset\"}");
    }
}

void handlePutPresetRename() {
    String oldName = server.pathArg(0);
    String newName = server.pathArg(1);
    oldName = urlDecode(oldName);
    newName = urlDecode(newName);

    // Check if old preset exists
    String oldFilename = "/presets/" + oldName + ".json";
    if (!LittleFS.exists(oldFilename)) {
        server.send(404, "application/json", "{\"error\":\"Preset not found\"}");
        return;
    }

    // Check if new name already exists
    String newFilename = "/presets/" + newName + ".json";
    if (LittleFS.exists(newFilename)) {
        server.send(400, "application/json", "{\"error\":\"New preset name already exists\"}");
        return;
    }

    // Read old preset
    File oldFile = LittleFS.open(oldFilename, "r");
    if (!oldFile) {
        server.send(500, "application/json", "{\"error\":\"Failed to read old preset\"}");
        return;
    }

    String presetData = oldFile.readString();
    oldFile.close();

    // Parse and update name
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, presetData);
    if (error) {
        server.send(500, "application/json", "{\"error\":\"Failed to parse preset data\"}");
        return;
    }

    doc["name"] = newName;

    // Write new preset
    File newFile = LittleFS.open(newFilename, "w");
    if (!newFile) {
        server.send(500, "application/json", "{\"error\":\"Failed to create new preset file\"}");
        return;
    }

    serializeJson(doc, newFile);
    newFile.close();

    // Delete old preset
    LittleFS.remove(oldFilename);

    // Update current preset if necessary
    if (systemSettings.currentPreset == oldName) {
        systemSettings.currentPreset = newName;
        scheduleConfigWrite();
    }

    // TODO: Communicate rename to Teensy if this is the active preset

    // Broadcast update
    DynamicJsonDocument response(256);
    response["event"] = "preset";
    response["action"] = "renamed";
    response["oldName"] = oldName;
    response["newName"] = newName;
    String wsMessage;
    serializeJson(response, wsMessage);
    broadcastWebSocket(wsMessage);


    server.send(200, "application/json", "{\"success\":true,\"name\":\"" + newName + "\"}");
}

void handleDeletePreset() {
    String name = server.pathArg(0);
    name = urlDecode(name);

    String filename = "/presets/" + name + ".json";
    if (!LittleFS.exists(filename)) {
        server.send(404, "application/json", "{\"error\":\"Preset not found\"}");
        return;
    }

    // Remove the preset file
    if (!LittleFS.remove(filename)) {
        server.send(500, "application/json", "{\"error\":\"Failed to delete preset\"}");
        return;
    }

    // If this was the current preset, clear it
    if (systemSettings.currentPreset == name) {
        systemSettings.currentPreset = "";
        scheduleConfigWrite();
        // TODO: Communicate preset deactivation to Teensy
    }

    // Broadcast update
    DynamicJsonDocument response(256);
    response["event"] = "preset";
    response["action"] = "deleted";
    response["name"] = name;
    String wsMessage;
    serializeJson(response, wsMessage);
    broadcastWebSocket(wsMessage);

    server.send(200, "application/json", "{\"success\":true,\"name\":\"" + name + "\"}");
}

// Speaker delay endpoints
void handlePutPresetDelay() {
    String speaker = server.pathArg(0);
    String msStr = server.pathArg(1);

    if (speaker != "left" && speaker != "right" && speaker != "sub") {
        server.send(400, "application/json", "{\"error\":\"Speaker must be left, right, or sub\"}");
        return;
    }

    float delayMs = msStr.toFloat();

    if (systemSettings.currentPreset.isEmpty()) {
        server.send(400, "application/json", "{\"error\":\"No current preset selected\"}");
        return;
    }

    // Load current preset
    String filename = "/presets/" + systemSettings.currentPreset + ".json";
    DynamicJsonDocument doc(2048);
    if (!loadPresetFromFile(filename, doc)) {
        server.send(500, "application/json", "{\"error\":\"Failed to load preset\"}");
        return;
    }

    // Update speaker delay
    doc["speakerDelays"][speaker] = delayMs;

    // Save preset
    if (!savePresetToFile(filename, doc)) {
        server.send(500, "application/json", "{\"error\":\"Failed to save preset\"}");
        return;
    }

    // TODO: Communicate speaker delay change to Teensy

    // Broadcast update
    DynamicJsonDocument response(256);
    response["event"] = "speaker_delay";
    response["speaker"] = speaker;
    response["delayMs"] = delayMs;
    response["preset"] = systemSettings.currentPreset;
    String wsMessage;
    serializeJson(response, wsMessage);
    broadcastWebSocket(wsMessage);


    server.send(200, "application/json", "{\"success\":true,\"speaker\":\"" + speaker + "\",\"delayMs\":" + String(delayMs) + "}");
}

void handlePutPresetDelayNamed() {
    String presetName = server.pathArg(0);
    String speaker = server.pathArg(1);
    String msStr = server.pathArg(2);
    presetName = urlDecode(presetName);

    if (speaker != "left" && speaker != "right" && speaker != "sub") {
        server.send(400, "application/json", "{\"error\":\"Speaker must be left, right, or sub\"}");
        return;
    }

    float delayMs = msStr.toFloat();

    // Load preset
    String filename = "/presets/" + presetName + ".json";
    DynamicJsonDocument doc(2048);
    if (!loadPresetFromFile(filename, doc)) {
        server.send(404, "application/json", "{\"error\":\"Preset not found\"}");
        return;
    }

    // Update speaker delay
    doc["speakerDelays"][speaker] = delayMs;

    // Save preset
    if (!savePresetToFile(filename, doc)) {
        server.send(500, "application/json", "{\"error\":\"Failed to save preset\"}");
        return;
    }

    // TODO: Communicate speaker delay change to Teensy if this is the active preset

    // Broadcast update
    DynamicJsonDocument response(256);
    response["event"] = "speaker_delay";
    response["speaker"] = speaker;
    response["delayMs"] = delayMs;
    response["preset"] = presetName;
    String wsMessage;
    serializeJson(response, wsMessage);
    broadcastWebSocket(wsMessage);

    server.send(200, "application/json", "{\"success\":true,\"speaker\":\"" + speaker + "\",\"delayMs\":" + String(delayMs) + ",\"preset\":\"" + presetName + "\"}");
}

// EQ management endpoints
void handlePostPresetEQ() {
    String presetName = server.pathArg(0);
    String type = server.pathArg(1);
    String splStr = server.pathArg(2);
    presetName = urlDecode(presetName);

    if (type != "room" && type != "pref") {
        server.send(400, "application/json", "{\"error\":\"Type must be room or pref\"}");
        return;
    }

    int spl = splStr.toInt();
    if (spl < 0 || spl > 120) {
        server.send(400, "application/json", "{\"error\":\"SPL must be between 0 and 120\"}");
        return;
    }

    // Parse request body
    DynamicJsonDocument requestDoc(1024);
    DeserializationError error = deserializeJson(requestDoc, server.arg("plain"));
    if (error) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON body\"}");
        return;
    }

    if (!requestDoc.is < JsonArray > ()) {
        server.send(400, "application/json", "{\"error\":\"Body must be an array of PEQ points\"}");
        return;
    }

    // Load preset
    String filename = "/presets/" + presetName + ".json";
    DynamicJsonDocument doc(2048);
    if (!loadPresetFromFile(filename, doc)) {
        server.send(404, "application/json", "{\"error\":\"Preset not found\"}");
        return;
    }

    // Find or create EQ entry for this SPL
    JsonArray eqArray = doc[type == "room" ? "roomCorrection" : "preferenceEQ"];
    JsonObject eqEntry;

    for (JsonObject entry: eqArray) {
        if (entry["spl"] == spl) {
            eqEntry = entry;
            break;
        }
    }

    if (eqEntry.isNull()) {
        eqEntry = eqArray.createNestedObject();
        eqEntry["spl"] = spl;
    }

    // Update PEQ set
    eqEntry["peqSet"] = requestDoc.as < JsonArray > ();

    // Save preset
    if (!savePresetToFile(filename, doc)) {
        server.send(500, "application/json", "{\"error\":\"Failed to save preset\"}");
        return;
    }

    // TODO: Communicate EQ changes to Teensy if this is the active preset

    // Broadcast update
    DynamicJsonDocument response(512);
    response["event"] = "eq";
    response["preset"] = presetName;
    response["type"] = type;
    response["spl"] = spl;
    response["peqSet"] = requestDoc.as < JsonArray > ();
    String wsMessage;
    serializeJson(response, wsMessage);
    broadcastWebSocket(wsMessage);

    server.send(200, "application/json", "{\"success\":true,\"preset\":\"" + presetName + "\",\"type\":\"" + type + "\",\"spl\":" + String(spl) + "}");
}

void handleDeletePresetEQ() {
    String presetName = server.pathArg(0);
    String type = server.pathArg(1);
    String splStr = server.pathArg(2);
    presetName = urlDecode(presetName);

    if (type != "room" && type != "pref") {
        server.send(400, "application/json", "{\"error\":\"Type must be room or pref\"}");
        return;
    }

    int spl = splStr.toInt();

    // Load preset
    String filename = "/presets/" + presetName + ".json";
    DynamicJsonDocument doc(2048);
    if (!loadPresetFromFile(filename, doc)) {
        server.send(404, "application/json", "{\"error\":\"Preset not found\"}");
        return;
    }

    // Find and remove EQ entry
    JsonArray eqArray = doc[type == "room" ? "roomCorrection" : "preferenceEQ"];
    bool found = false;

    for (size_t i = 0; i < eqArray.size(); i++) {
        if (eqArray[i]["spl"] == spl) {
            eqArray.remove(i);
            found = true;
            break;
        }
    }

    if (!found) {
        server.send(404, "application/json", "{\"error\":\"EQ configuration not found\"}");
        return;
    }

    // Save preset
    if (!savePresetToFile(filename, doc)) {
        server.send(500, "application/json", "{\"error\":\"Failed to save preset\"}");
        return;
    }

    // TODO: Communicate EQ changes to Teensy if this is the active preset

    // Broadcast update
    DynamicJsonDocument response(256);
    response["event"] = "eq_deleted";
    response["preset"] = presetName;
    response["type"] = type;
    response["spl"] = spl;
    String wsMessage;
    serializeJson(response, wsMessage);
    broadcastWebSocket(wsMessage);

    server.send(200, "application/json", "{\"success\":true,\"preset\":\"" + presetName + "\",\"type\":\"" + type + "\",\"spl\":" + String(spl) + "}");
}

// Crossover endpoint
void handlePutPresetCrossover() {
    String presetName = server.pathArg(0);
    String freqStr = server.pathArg(1);
    String slope = server.pathArg(2);
    presetName = urlDecode(presetName);

    int frequency = freqStr.toInt();
    if (frequency < 40 || frequency > 150) {
        server.send(400, "application/json", "{\"error\":\"Frequency must be between 40 and 150 Hz\"}");
        return;
    }

    if (slope != "12" && slope != "24") {
        server.send(400, "application/json", "{\"error\":\"Slope must be 12 or 24\"}");
        return;
    }

    // Load preset
    String filename = "/presets/" + presetName + ".json";
    DynamicJsonDocument doc(2048);
    if (!loadPresetFromFile(filename, doc)) {
        server.send(404, "application/json", "{\"error\":\"Preset not found\"}");
        return;
    }

    // Update crossover
    doc["crossover"]["frequency"] = frequency;
    doc["crossover"]["slope"] = slope;

    // Save preset
    if (!savePresetToFile(filename, doc)) {
        server.send(500, "application/json", "{\"error\":\"Failed to save preset\"}");
        return;
    }

    // TODO: Communicate crossover changes to Teensy if this is the active preset

    // Broadcast update
    DynamicJsonDocument response(256);
    response["event"] = "crossover";
    response["preset"] = presetName;
    response["frequency"] = frequency;
    response["slope"] = slope;
    String wsMessage;
    serializeJson(response, wsMessage);
    broadcastWebSocket(wsMessage);

    server.send(200, "application/json", "{\"success\":true,\"preset\":\"" + presetName + "\",\"frequency\":" + String(frequency) + ",\"slope\":\"" + slope + "\"}");
}

// Equal loudness endpoint
void handlePutPresetEqualLoudness() {
    String presetName = server.pathArg(0);
    String state = server.pathArg(1);
    presetName = urlDecode(presetName);

    if (state != "on" && state != "off") {
        server.send(400, "application/json", "{\"error\":\"State must be on or off\"}");
        return;
    }

    // Load preset
    String filename = "/presets/" + presetName + ".json";
    DynamicJsonDocument doc(2048);
    if (!loadPresetFromFile(filename, doc)) {
        server.send(404, "application/json", "{\"error\":\"Preset not found\"}");
        return;
    }

    // Update equal loudness
    doc["equalLoudness"] = (state == "on");

    // Save preset
    if (!savePresetToFile(filename, doc)) {
        server.send(500, "application/json", "{\"error\":\"Failed to save preset\"}");
        return;
    }

    // TODO: Communicate equal loudness changes to Teensy if this is the active preset

    // Broadcast update
    DynamicJsonDocument response(256);
    response["event"] = "equal_loudness";
    response["preset"] = presetName;
    response["state"] = state;
    String wsMessage;
    serializeJson(response, wsMessage);
    broadcastWebSocket(wsMessage);

    server.send(200, "application/json", "{\"success\":true,\"preset\":\"" + presetName + "\",\"state\":\"" + state + "\"}");
}

// Set active preset
void handlePutActivePreset() {
    String name = server.pathArg(0);
    name = urlDecode(name);

    String filename = "/presets/" + name + ".json";
    if (!LittleFS.exists(filename)) {
        server.send(404, "application/json", "{\"error\":\"Preset not found\"}");
        return;
    }

    systemSettings.currentPreset = name;
    scheduleConfigWrite();

    // TODO: Communicate preset activation to Teensy
    // This would include sending all the preset's settings:
    // - Speaker delays
    // - Crossover settings
    // - EQ configurations (room correction + preference curve)
    // - Equal loudness setting

    // Broadcast update
    DynamicJsonDocument response(256);
    response["event"] = "preset";
    response["action"] = "activated";
    response["name"] = name;
    String wsMessage;
    serializeJson(response, wsMessage);
    broadcastWebSocket(wsMessage);


    server.send(200, "application/json", "{\"success\":true,\"activePreset\":\"" + name + "\"}");
}

void handleFileServing() {
    // Implement file serving logic here
    server.send(404, "text/plain", "Not Found");
}


// URL decoding utility
String urlDecode(String str) {
    String decodedString = "";
    char c;
    char code0;
    char code1;
    for (unsigned int i = 0; i < str.length(); i++) {
        c = str.charAt(i);
        if (c == '+') {
            decodedString += ' ';
        } else if (c == '%') {
            i++;
            code0 = str.charAt(i);
            i++;
            code1 = str.charAt(i);
            c = (h2int(code0) << 4) | h2int(code1);
            decodedString += c;
        } else {
            decodedString += c;
        }
    }
    return decodedString;
}

unsigned char h2int(char c) {
    if (c >= '0' && c <= '9') {
        return ((unsigned char) c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return ((unsigned char) c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return ((unsigned char) c - 'A' + 10);
    }
    return (0);
}