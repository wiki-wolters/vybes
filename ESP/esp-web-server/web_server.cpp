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
#include "api_volume.h"
#include "api_helpers.h"
#include "teensy_comm.h"
#include "config.h"
#include <ArduinoJson.h>

// Both listeners serve identical routes. HTTPS exists so browsers grant
// microphone access to the analyzer page (getUserMedia requires a secure
// origin). It is only built on the ESP32-S3 - the classic ESP32 doesn't have
// the RAM for TLS - and only starts when certificates are present on
// LittleFS: generate them with ESP/make-certs.sh, upload `pio run -t uploadfs`.
PsychicHttpServer server;
#ifdef CONFIG_IDF_TARGET_ESP32S3
PsychicHttpsServer serverHttps;

static const char* CERT_PATH = "/certs/server.crt";
static const char* KEY_PATH = "/certs/server.key";

// PEM data must stay alive for the lifetime of the TLS server
static String serverCert;
static String serverKey;

static bool loadCertificates() {
    File crt = LittleFS.open(CERT_PATH, "r");
    File key = LittleFS.open(KEY_PATH, "r");
    if (!crt || !key) {
        if (crt) crt.close();
        if (key) key.close();
        return false;
    }
    serverCert = crt.readString();
    serverKey = key.readString();
    crt.close();
    key.close();
    return serverCert.length() > 0 && serverKey.length() > 0;
}
#endif // CONFIG_IDF_TARGET_ESP32S3

static esp_err_t handleBackup(PsychicRequest *request) {
    PsychicFileResponse response(request, LittleFS, CONFIG_FILE, "application/msgpack", true);
    return response.send();
}

// --- /restore upload ---
// Chunks stream into a temp file; validation and the reply happen in the
// completion handler so a bad/aborted upload can't destroy the live config.
static const char* RESTORE_TMP = "/config.restore";
// A config backup is a few KB - anything near this limit is not one.
static const uint64_t RESTORE_MAX_SIZE = 64 * 1024;
// A restore that hasn't completed within this window was aborted mid-upload
// (completion handler never ran); let a new one take over.
static const unsigned long RESTORE_STALE_MS = 30000;

// Shared across the listeners - the in-progress flag (guarded by the config
// mutex) rejects a second concurrent restore so they can't interleave.
static File restoreFile;
static bool restoreError;
static bool restoreInProgress = false;
static unsigned long restoreStartedAt = 0;

static esp_err_t handleRestoreUpload(PsychicRequest *request, const String& filename,
                                     uint64_t index, uint8_t *data, size_t len, bool last) {
    if (index == 0) {
        bool busy;
        {
            ConfigLock lock;
            busy = restoreInProgress && (millis() - restoreStartedAt < RESTORE_STALE_MS);
            if (!busy) {
                restoreInProgress = true;
                restoreStartedAt = millis();
            }
        }
        if (busy) {
            DebugSerial.println("Restore rejected: another restore is in progress");
            return ESP_FAIL; // aborts the request with an error response
        }
        DebugSerial.printf("Restore started: %s\n", filename.c_str());
        restoreError = false;
        restoreFile = LittleFS.open(RESTORE_TMP, "w");
        if (!restoreFile) {
            restoreError = true;
        }
    }

    if (!restoreError && index + len > RESTORE_MAX_SIZE) {
        DebugSerial.println("Restore aborted: upload exceeds size limit");
        restoreFile.close();
        LittleFS.remove(RESTORE_TMP);
        ConfigLock lock;
        restoreInProgress = false;
        return ESP_FAIL; // aborts the request with an error response
    }

    if (!restoreError && len) {
        if (restoreFile.write(data, len) != len) {
            restoreError = true;
            restoreFile.close();
            LittleFS.remove(RESTORE_TMP);
        }
    }

    if (last && restoreFile) {
        restoreFile.close();
        DebugSerial.printf("Restore finished: %s, %llu B\n", filename.c_str(), index + len);
    }
    return ESP_OK;
}

static esp_err_t finishRestore(PsychicRequest *request) {
    if (restoreError) {
        return request->reply(500, "text/plain", "Error writing uploaded configuration");
    }

    // Validate by loading it. On failure the previous config file is
    // untouched; reload it to undo any partial changes to current_config.
    // Hold the config lock so a debounced save can't serialize a half-loaded
    // config while we swap current_config around.
    {
        ConfigLock lock;
        if (!load_config_from(RESTORE_TMP)) {
            LittleFS.remove(RESTORE_TMP);
            load_config();
            return request->reply(400, "text/plain", "Invalid configuration file - restore aborted");
        }
    }

    // Valid: make it the live config file
    if (!LittleFS.rename(RESTORE_TMP, CONFIG_FILE)) {
        // Some FS implementations refuse to rename over an existing file
        LittleFS.remove(CONFIG_FILE);
        if (!LittleFS.rename(RESTORE_TMP, CONFIG_FILE)) {
            DebugSerial.println("Failed to move restored config into place");
            return request->reply(500, "text/plain", "Failed to move restored configuration into place");
        }
    }

    // Apply the restored state to the DSP
    updateTeensyWithActivePresetParameters();
    loadFirFilters();

    return request->reply(200, "text/plain", "Configuration restored successfully.");
}

static esp_err_t handleRestoreComplete(PsychicRequest *request) {
    esp_err_t result = finishRestore(request);
    ConfigLock lock;
    restoreInProgress = false;
    return result;
}

// Register every route on the given server. Called once per listener - the
// handlers are shared, endpoints (and the websocket handler) are per-server.
static void registerRoutes(PsychicHttpServer &s, PsychicWebSocketHandler *ws) {
    // API Routes - System Status
    s.on("/status", HTTP_GET, handleGetStatus);
    s.on("/mute/percent", HTTP_PUT, handlePutMutePercent);
    s.on("/mute", HTTP_PUT, handlePutMute);
    s.on("/volume", HTTP_PUT, handlePutVolume);

    // API Routes - Speaker & Input gains (JSON-body endpoints use the
    // PsychicJsonRequestCallback overload)
    s.on("/gains/speaker", HTTP_PUT, handlePutSpeakerGain);
    s.on("/gains/input", HTTP_PUT, (PsychicJsonRequestCallback)handlePutInputGains);

    s.on("/preset/gains", HTTP_GET, handleGetPresetGains);
    s.on("/preset/gains", HTTP_PUT, (PsychicJsonRequestCallback)handleSetPresetGains);

    // API Routes - FIR Filter Management
    s.on("/fir/files", HTTP_GET, handleGetFirFiles);
    s.on("/preset/fir/enabled", HTTP_PUT, handlePutPresetFirEnabled);
    s.on("/preset/fir", HTTP_PUT, handlePutPresetFir);

    // API Routes - Signal Generator
    s.on("/generate/tone/stop", HTTP_PUT, handlePutToneStop);
    s.on("/generate/tone", HTTP_PUT, handlePutTone);
    s.on("/noise", HTTP_PUT, handlePutNoise);

    s.on("/preset/active", HTTP_PUT, handlePutActivePreset);

    // Feature enablement
    s.on("/preset/delay/enabled", HTTP_PUT, handlePutPresetDelayEnabled);
    s.on("/preset/eq/enabled", HTTP_PUT, handlePutPresetEQEnabled);
    s.on("/preset/crossover/enabled", HTTP_PUT, handlePutPresetCrossoverEnabled);

    // API Routes - Speaker Configuration
    s.on("/preset/delay", HTTP_PUT, handlePutPresetDelayNamed);

    s.on("/preset/eq", HTTP_PUT, (PsychicJsonRequestCallback)handlePutPresetEQPoints);
    s.on("/preset/eq/point", HTTP_PUT, (PsychicJsonRequestCallback)handlePutPresetEQPoint);

    // API Routes - Crossover
    s.on("/preset/crossover", HTTP_PUT, handlePutPresetCrossover);

    // API Routes - Preset Management
    s.on("/presets", HTTP_GET, handleGetPresets);
    s.on("/preset", HTTP_DELETE, handleDeletePreset);
    s.on("/preset", HTTP_GET, handleGetPreset);

    s.on("/preset", HTTP_POST, [](PsychicRequest *request) {
        if (request->hasParam("action")) {
            String action = request->getParam("action")->value();
            if (action == "create") {
                return handlePostPresetCreate(request);
            } else if (action == "copy") {
                return handlePostPresetCopy(request);
            }
        }
        return request->reply(400, "text/plain", "Missing or unknown action");
    });

    s.on("/preset", HTTP_PUT, [](PsychicRequest *request) {
        if (request->hasParam("action")) {
            String action = request->getParam("action")->value();
            if (action == "rename") {
                return handlePutPresetRename(request);
            }
        }
        return request->reply(400, "text/plain", "Missing or unknown action");
    });

    // API Routes - Backup and Restore
    s.on("/backup", HTTP_GET, handleBackup);
    s.maxUploadSize = RESTORE_MAX_SIZE; // rejects oversized Content-Lengths up front
    PsychicUploadHandler *restoreHandler = new PsychicUploadHandler();
    restoreHandler->onUpload(handleRestoreUpload);
    restoreHandler->onRequest(handleRestoreComplete);
    s.on("/restore", HTTP_POST, restoreHandler);

    // Live updates websocket (one handler per listener - see websocket.h)
    s.on("/live-updates", ws);

    // Static assets: hashed filenames under /assets cache forever; the rest
    // of the dist (index.html, favicon, manifest, icons) serves from root.
    s.serveStatic("/assets", LittleFS, "/dist/assets/", "public, max-age=31536000, immutable");
    s.serveStatic("/", LittleFS, "/dist/")->setDefaultFile("index.html");

    s.onNotFound([](PsychicRequest *request) {
        return request->reply(404);
    });
}

void setupWebServer() {
    // ~35 routes per listener (esp-idf's default cap is 8)
    server.config.max_uri_handlers = 60;
    server.listen(80);
    registerRoutes(server, &wsHttp);
    DebugSerial.println("HTTP server started on port 80");

#ifdef CONFIG_IDF_TARGET_ESP32S3
    if (loadCertificates()) {
        serverHttps.ssl_config.httpd.max_uri_handlers = 60;
        // Each TLS connection costs ~45KB of heap - keep the count low
        serverHttps.ssl_config.httpd.max_open_sockets = 3;
        serverHttps.listen(443, serverCert.c_str(), serverKey.c_str());
        registerRoutes(serverHttps, &wsHttps);
        DebugSerial.println("HTTPS server started on port 443");
    } else {
        DebugSerial.println("No certificates in /certs - HTTPS disabled (see ESP/make-certs.sh)");
    }
#else
    DebugSerial.println("HTTPS not built on this target (ESP32-S3 only)");
#endif
}
