#include "globals.h"
#include "web_server.h"
#include "websocket.h"
#include "file_system.h"
#include "api_system.h"
#include "api_signal_generator.h"
#include "api_probe.h"
#include "api_gains.h"
#include "api_fir.h"
#include "api_presets.h"
#include "api_preset_config.h"
#include "api_outputs.h"
#include "api_volume.h"
#include "api_recorder.h"
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

    // Applying a restored config triggers a FIR load on the Teensy, whose SD
    // reads would stall its loop() past what the record queues can buffer.
    if (isRecordingActive()) {
        return request->reply(409, "text/plain", "Restore is locked while recording");
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
    s.on("/device/name", HTTP_PUT, handlePutDeviceName);
    s.on("/mute/percent", HTTP_PUT, handlePutMutePercent);
    s.on("/mute", HTTP_PUT, handlePutMute);
    s.on("/volume", HTTP_PUT, handlePutVolume);

    // API Routes - Speaker & Input gains (JSON-body endpoints use the
    // PsychicJsonRequestCallback overload). /gains/speaker survives for the
    // remote/button path until that is reworked; the UI no longer calls it.
    s.on("/gains/speaker", HTTP_PUT, handlePutSpeakerGain);
    s.on("/gains/input", HTTP_PUT, (PsychicJsonRequestCallback)handlePutInputGains);

    // API Routes - FIR Filter Management
    s.on("/fir/files", HTTP_GET, handleGetFirFiles);
    s.on("/preset/fir/enabled", HTTP_PUT, handlePutPresetFirEnabled);
    s.on("/preset/fir/pool", HTTP_GET, handleGetPresetFirPool);

    // API Routes - Signal Generator
    s.on("/generate/tone/stop", HTTP_PUT, handlePutToneStop);
    s.on("/generate/tone", HTTP_PUT, handlePutTone);
    s.on("/noise", HTTP_PUT, handlePutNoise);

    // API Routes - Auto delay alignment probe
    s.on("/probe/delay/start", HTTP_PUT, handlePutProbeDelayStart);
    s.on("/probe/delay/stop", HTTP_PUT, handlePutProbeDelayStop);

    s.on("/preset/active", HTTP_PUT, handlePutActivePreset);

    // Feature enablement
    s.on("/preset/delay/enabled", HTTP_PUT, handlePutPresetDelayEnabled);
    s.on("/preset/eq/enabled", HTTP_PUT, handlePutPresetEQEnabled);
    s.on("/preset/crossover/enabled", HTTP_PUT, handlePutPresetCrossoverEnabled);

    // API Routes - Input EQ (shared L/R preference curve + SPL sets)
    s.on("/preset/eq", HTTP_PUT, (PsychicJsonRequestCallback)handlePutPresetEQPoints);
    s.on("/preset/eq/point", HTTP_PUT, (PsychicJsonRequestCallback)handlePutPresetEQPoint);

    // API Routes - Crossover points
    s.on("/preset/crossover", HTTP_PUT, handlePutPresetCrossover);

    // API Routes - Dynamics (mixed-input multiband compressor)
    s.on("/preset/dynamics", HTTP_PUT, (PsychicJsonRequestCallback)handlePutPresetDynamics);
    s.on("/comp/solo", HTTP_POST, handlePostCompSolo);

    // API Routes - Output channels (V1)
    s.on("/preset/output/label", HTTP_PUT, handlePutOutputLabel);
    s.on("/preset/output/enabled", HTTP_PUT, handlePutOutputEnabled);
    s.on("/preset/output/source", HTTP_PUT, (PsychicJsonRequestCallback)handlePutOutputSource);
    s.on("/preset/output/gain", HTTP_PUT, handlePutOutputGain);
    s.on("/preset/output/mute", HTTP_PUT, handlePutOutputMute);
    s.on("/preset/output/invert", HTTP_PUT, handlePutOutputInvert);
    s.on("/preset/output/delay", HTTP_PUT, handlePutOutputDelay);
    s.on("/preset/output/filter", HTTP_PUT, (PsychicJsonRequestCallback)handlePutOutputFilter);
    s.on("/preset/output/eq", HTTP_PUT, (PsychicJsonRequestCallback)handlePutOutputEq);
    s.on("/preset/output/eq/point", HTTP_PUT, (PsychicJsonRequestCallback)handlePutOutputEqPoint);
    s.on("/preset/output/eq/enabled", HTTP_PUT, handlePutOutputEqEnabled);
    s.on("/preset/output/fir", HTTP_PUT, handlePutOutputFir);

    // API Routes - Preset Management
    s.on("/templates", HTTP_GET, handleGetTemplates);
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

    // API Routes - SD recorder / player
    s.on("/recorder/record/start", HTTP_POST, handlePostRecordStart);
    s.on("/recorder/record/stop", HTTP_POST, handlePostRecordStop);
    s.on("/recorder/play/stop", HTTP_POST, handlePostRecorderPlayStop);
    s.on("/recorder/play", HTTP_POST, handlePostRecorderPlay);
    s.on("/recorder/file", HTTP_DELETE, handleDeleteRecording);
    s.on("/recorder", HTTP_GET, handleGetRecorder);

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
    // esp-idf defaults this to 7, which was never budgeted against
    // CONFIG_LWIP_MAX_SOCKETS=16: 7 + listener + ctrl here, plus 3 + listener
    // + ctrl on the HTTPS side, plus mDNS and DHCP, runs the socket table to
    // its limit before heap ever becomes the constraint. Plain HTTP sockets
    // are cheap in heap (no TLS buffers), so 4 costs nothing and keeps a
    // browser's six parallel keep-alive connections from squeezing lwIP.
    server.config.max_open_sockets = 4;
    // esp-idf defaults the server task stack to 4096, which POST /restore
    // overflows: the multipart parser is already ~3.4KB deep when the
    // completion handler runs, and load_config_from's msgpack deserialize
    // peaks the task at ~4.1KB. The overflow panicked the device mid-request
    // (client saw a hung socket, config was left untouched) - measured
    // 2026-08-17. esp-idf gives its own TLS listener 10240
    // (HTTPD_SSL_CONFIG_DEFAULT), which is why the same restore always
    // worked over HTTPS; both listeners serve identical routes, so match it.
    server.config.stack_size = 10240;
    // Evict the least-recently-active connection instead of refusing new
    // ones - browsers park idle keep-alive sockets that would otherwise
    // starve the listener. An idle live-updates websocket can be the
    // eviction victim during a fetch burst; the client auto-reconnects
    // within ~1s, which beats hard-refusing the burst.
    server.config.lru_purge_enable = true;
    server.listen(80);
    registerRoutes(server, &wsHttp);
    DebugSerial.println("HTTP server started on port 80");

#ifdef CONFIG_IDF_TARGET_ESP32S3
    if (loadCertificates()) {
        serverHttps.ssl_config.httpd.max_uri_handlers = 60;
        // Each open TLS connection costs ~40KB at peak, so this is a heap
        // budget more than a concurrency limit. Measured 2026-08-15 against
        // a live device: 132,444 bytes free idle, 92,668 while serving one
        // HTTPS request. 6 sockets (tried 2026-08-09) let a 6-fetch burst
        // exhaust the heap and wedge lwIP until a hardware reset - the
        // device stopped answering even ping. 4 was meant to fix that but
        // still budgets ~160KB against ~132KB free, and the same wedge
        // recurred on 2026-08-15. 3 is the first setting that actually fits.
        // There is no leak: heap returns to ~132.1KB after every connection,
        // so only a concurrency burst can trigger it. The page survives
        // losing the race because the stylesheet is inlined into index.html
        // (see WebUI/vite.config.js); only cosmetic fetches (icons/manifest)
        // can fail. Check /status freeHeap before ever raising this.
        serverHttps.ssl_config.httpd.max_open_sockets = 3;
        // Same LRU eviction as the HTTP listener - vital here, where the
        // socket budget is this tight.
        serverHttps.ssl_config.httpd.lru_purge_enable = true;
        // Every esp-idf httpd instance needs its own control socket; the
        // default (32768) is already taken by the HTTP listener above
        serverHttps.ssl_config.httpd.ctrl_port = 32769;
        // stack_size is left at HTTPD_SSL_CONFIG_DEFAULT's 10240 - see the
        // note on the HTTP listener's stack above.
        if (serverHttps.listen(443, serverCert.c_str(), serverKey.c_str()) == ESP_OK) {
            registerRoutes(serverHttps, &wsHttps);
            DebugSerial.println("HTTPS server started on port 443");
        } else {
            DebugSerial.println("HTTPS server failed to start");
        }
    } else {
        DebugSerial.println("No certificates in /certs - HTTPS disabled (see ESP/make-certs.sh)");
    }
#else
    DebugSerial.println("HTTPS not built on this target (ESP32-S3 only)");
#endif
}
