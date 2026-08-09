#include "globals.h"
#include "teensy_comm.h"
#include "config.h"
#include "websocket.h"
#include "compare_mode.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Outgoing queue. A full V1 preset sync is ~190 commands worst case
// (8 outputs x up to 19 commands each, plus input EQ, dynamics and globals).
#define QUEUE_SIZE 220
// Incoming line assembly. Sized for the longest line the Teensy sends: a
// 121-band RTA frame ("RTA " + 242 hex chars = 246 chars).
#define RX_LINE_MAX 300
// Cached SD file list (newline separated "name size" lines)
#define FIR_CACHE_MAX 1024
// Heartbeat: detects a Teensy reboot even if its boot event was missed
#define PING_INTERVAL_MS 5000

struct QueuedCommand {
    char msg[TEENSY_MSG_MAX];     // full message incl. trailing newline; "" = cancelled slot
};

static QueuedCommand cmdQueue[QUEUE_SIZE];
static uint8_t queueHead = 0;     // index of next command to send
static uint8_t queueCount = 0;

// Guards cmdQueue/queueHead/queueCount: commands are enqueued from the two
// httpd server tasks (API handlers) and the loop task (heartbeat, RTA
// keepalive) while the loop task drains. Created in initTeensyComm, which
// must run before the web servers start.
static SemaphoreHandle_t queueMutex = nullptr;

// RX state
static char rxLine[RX_LINE_MAX];
static size_t rxLen = 0;
static bool rxOverflow = false;

// FIR file list cache, filled asynchronously from "FILES ... EOT" replies.
// Written by the loop task, read by the httpd tasks - firCacheMutex guards it.
static char firFilesCache[FIR_CACHE_MAX] = {0};
static SemaphoreHandle_t firCacheMutex = nullptr;
static char firFilesPending[FIR_CACHE_MAX];
static size_t firFilesPendingLen = 0;
static bool collectingFiles = false;

// --- Message building ---

// The actual formatting lives in teensy_protocol.h (shared with the Teensy
// test suite); this wrapper just adds the debug log on truncation.
static size_t buildMessage(char* out, size_t outSize, const char* command,
                           const char* p1, const char* p2, const char* p3, const char* p4,
                           const char* p5) {
    bool truncated = false;
    size_t offset = teensyBuildMessage(out, outSize, command, p1, p2, p3, p4, p5, &truncated);
    if (truncated) {
        DebugSerial.print("Teensy command truncated: ");
        DebugSerial.println(out);
    }
    return offset;
}

// --- Coalescing ---

// Extract up to three leading space-separated tokens of a message.
static void firstTokens(const char* msg, char* t1, size_t s1, char* t2, size_t s2,
                        char* t3, size_t s3) {
    char* tokens[3] = {t1, t2, t3};
    size_t sizes[3] = {s1, s2, s3};
    size_t i = 0;
    for (int t = 0; t < 3; t++) {
        size_t j = 0;
        while (msg[i] && msg[i] != ' ' && msg[i] != '\n' && j < sizes[t] - 1) {
            tokens[t][j++] = msg[i++];
        }
        tokens[t][j] = '\0';
        if (msg[i] != ' ') {
            for (int rest = t + 1; rest < 3; rest++) tokens[rest][0] = '\0';
            break;
        }
        i++;
    }
}

// How many leading tokens (including the command itself) identify the
// parameter a message sets. Channel-indexed commands are keyed by their
// channel/band argument, setOutputEq by channel AND band.
static int coalesceKeyTokens(const char* command) {
    if (strcmp(command, CMD_SET_OUTPUT_EQ) == 0) return 3;
    if (strncmp(command, "setOutput", 9) == 0 ||
        strcmp(command, CMD_RESET_OUTPUT_EQ) == 0 ||
        strcmp(command, CMD_SET_FIR) == 0 ||
        strcmp(command, CMD_SET_INPUT_EQ) == 0 ||
        strcmp(command, CMD_SET_COMP_BAND) == 0 ||
        strcmp(command, CMD_SET_COMP_BAND_BYPASS) == 0) {
        return 2;
    }
    return 1;
}

// Two messages coalesce when they set the same parameter: same command and
// same identifying arguments. The newer message replaces the older one in
// place, preserving queue order.
static bool coalesces(const char* a, const char* b) {
    char a1[24], a2[24], a3[24], b1[24], b2[24], b3[24];
    firstTokens(a, a1, sizeof(a1), a2, sizeof(a2), a3, sizeof(a3));
    firstTokens(b, b1, sizeof(b1), b2, sizeof(b2), b3, sizeof(b3));
    if (strcmp(a1, b1) != 0) return false;
    int keyTokens = coalesceKeyTokens(a1);
    if (keyTokens >= 2 && strcmp(a2, b2) != 0) return false;
    if (keyTokens >= 3 && strcmp(a3, b3) != 0) return false;
    return true;
}

// An EQ reset cancels any queued point-set it supersedes, so a stale pending
// point can't re-enable a band the reset just disabled:
//   "resetInputEq N"      cancels "setInputEq band…"      with band >= N
//   "resetOutputEq CH N"  cancels "setOutputEq CH band…"  with band >= N
static void cancelSupersededEqCommands(const char* resetMsg) {
    char t1[24], t2[24], t3[24];
    firstTokens(resetMsg, t1, sizeof(t1), t2, sizeof(t2), t3, sizeof(t3));
    bool perOutput = strcmp(t1, CMD_RESET_OUTPUT_EQ) == 0;
    const char* setCommand = perOutput ? CMD_SET_OUTPUT_EQ : CMD_SET_INPUT_EQ;
    int fromIndex = atoi(perOutput ? t3 : t2);
    for (uint8_t i = 0; i < queueCount; i++) {
        QueuedCommand& e = cmdQueue[(queueHead + i) % QUEUE_SIZE];
        if (e.msg[0] == '\0') continue;
        char e1[24], e2[24], e3[24];
        firstTokens(e.msg, e1, sizeof(e1), e2, sizeof(e2), e3, sizeof(e3));
        if (strcmp(e1, setCommand) != 0) continue;
        if (perOutput) {
            if (strcmp(e2, t2) == 0 && atoi(e3) >= fromIndex) e.msg[0] = '\0';
        } else if (atoi(e2) >= fromIndex) {
            e.msg[0] = '\0'; // cancel; drained slots are skipped
        }
    }
}

static bool enqueueMessage(const char* msg) {
    // Replace an existing entry for the same parameter if there is one
    for (uint8_t i = 0; i < queueCount; i++) {
        QueuedCommand& e = cmdQueue[(queueHead + i) % QUEUE_SIZE];
        if (e.msg[0] != '\0' && coalesces(e.msg, msg)) {
            strlcpy(e.msg, msg, sizeof(e.msg));
            return true;
        }
    }
    if (queueCount >= QUEUE_SIZE) {
        DebugSerial.print("Teensy queue full - dropping: ");
        DebugSerial.print(msg);
        return false;
    }
    QueuedCommand& e = cmdQueue[(queueHead + queueCount) % QUEUE_SIZE];
    strlcpy(e.msg, msg, sizeof(e.msg));
    queueCount++;
    return true;
}

// --- Public send API ---

bool sendToTeensy(const char* command, const char* param1, const char* param2,
                  const char* param3, const char* param4, const char* param5) {
    char message[TEENSY_MSG_MAX];
    buildMessage(message, sizeof(message), command, param1, param2, param3, param4, param5);
    xSemaphoreTake(queueMutex, portMAX_DELAY);
    if (strcmp(command, CMD_RESET_INPUT_EQ) == 0 || strcmp(command, CMD_RESET_OUTPUT_EQ) == 0) {
        cancelSupersededEqCommands(message);
    }
    bool queued = enqueueMessage(message);
    xSemaphoreGive(queueMutex);
    return queued;
}

bool sendToTeensy(const char* command, const String& param1,
                  const String& param2, const String& param3, const String& param4,
                  const String& param5) {
    return sendToTeensy(
        command,
        param1.length() > 0 ? param1.c_str() : nullptr,
        param2.length() > 0 ? param2.c_str() : nullptr,
        param3.length() > 0 ? param3.c_str() : nullptr,
        param4.length() > 0 ? param4.c_str() : nullptr,
        param5.length() > 0 ? param5.c_str() : nullptr
    );
}

void sendOnOffToTeensy(const char* command, bool on) {
    sendToTeensy(command, on ? "1" : "0", nullptr);
}

void sendIntToTeensy(const char* command, int value) {
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%d", value);
    sendToTeensy(command, buffer, nullptr);
}

void sendFloatToTeensy(const char* command, float value) {
    char buffer[32];
    dtostrf(value, 1, 2, buffer); // 1 minimum width, 2 decimal places
    sendToTeensy(command, buffer, nullptr);
}

void sendStringToTeensy(const char* command, const char* value) {
    sendToTeensy(command, value, nullptr);
}

void sendStringToTeensy(const char* command, const String& value) {
    sendToTeensy(command, value.c_str(), nullptr);
}

// --- FIR file cache ---

size_t copyCachedFirFiles(char* dst, size_t dstSize) {
    xSemaphoreTake(firCacheMutex, portMAX_DELAY);
    size_t len = strlcpy(dst, firFilesCache, dstSize);
    xSemaphoreGive(firCacheMutex);
    return len;
}

long getCachedFirFileSize(const char* name) {
    size_t nameLen = strlen(name);
    if (nameLen == 0) return -1;

    long size = -1;
    xSemaphoreTake(firCacheMutex, portMAX_DELAY);
    const char* line = firFilesCache;
    while (*line != '\0') {
        const char* end = strchr(line, '\n');
        size_t lineLen = end ? (size_t)(end - line) : strlen(line);
        // A line is "name" (old firmware) or "name size"; filenames can't
        // contain spaces (enforced on upload), so the first token is the name
        if (lineLen > nameLen && strncmp(line, name, nameLen) == 0 && line[nameLen] == ' ') {
            size = strtol(line + nameLen + 1, nullptr, 10);
            if (size < 0) size = -1;
            break;
        }
        if (lineLen == nameLen && strncmp(line, name, nameLen) == 0) {
            break; // listed without a size: known file, unknown size
        }
        if (!end) break;
        line = end + 1;
    }
    xSemaphoreGive(firCacheMutex);
    return size;
}

void requestFirFilesRefresh() {
    sendToTeensy(CMD_GET_FILES, nullptr);
}

// --- RX line handling ---

// Handle one complete line from the Teensy. The Teensy sends:
//   "EVENT boot"        on startup (triggers a full state re-sync)
//   "PONG <uptimeMs>"   in reply to ping (reboot detection fallback)
//   "FILES" ... "EOT"   the SD file list, one filename per line
// Anything else is forwarded to the debug console.
static void handleTeensyLine(const char* line) {
    // RTA spectrum frames stream at ~10Hz while the analyzer UI is open -
    // forward them straight to the websocket, no debug logging.
    if (strncmp(line, "RTA ", 4) == 0) {
        broadcastRtaFrame(line + 4);
        return;
    }
    if (strncmp(line, "GRM ", 4) == 0) {
        broadcastGrmFrame(line + 4);
        return;
    }

    // Delay-probe progress lines ("PROBE START ...", "PROBE CHIRP ...",
    // "PROBE DONE", ...) - relay to the web UI, which drives its alignment
    // wizard off them.
    if (strncmp(line, "PROBE ", 6) == 0) {
        broadcastProbeEvent(line + 6);
        return;
    }

    // "FIRGAIN <ch> <centi-dB>": pink-weighted gain of a loaded FIR filter,
    // sent after every loadFirFiles (and on getFirGains). Comparison mode
    // uses these to level-match FIR on/off states.
    if (strncmp(line, "FIRGAIN ", 8) == 0) {
        char* end = nullptr;
        long ch = strtol(line + 8, &end, 10);
        if (end != nullptr && *end == ' ') {
            compareSetFirGain((int)ch, (int)strtol(end + 1, nullptr, 10));
        }
        return;
    }

    if (collectingFiles) {
        if (strcmp(line, "EOT") == 0) {
            xSemaphoreTake(firCacheMutex, portMAX_DELAY);
            memcpy(firFilesCache, firFilesPending, firFilesPendingLen);
            firFilesCache[firFilesPendingLen] = '\0';
            xSemaphoreGive(firCacheMutex);
            collectingFiles = false;
            DebugSerial.println("FIR file list updated");
        } else {
            size_t len = strlen(line);
            if (firFilesPendingLen + len + 1 < sizeof(firFilesPending)) {
                memcpy(firFilesPending + firFilesPendingLen, line, len);
                firFilesPendingLen += len;
                firFilesPending[firFilesPendingLen++] = '\n';
            }
        }
        return;
    }

    if (strcmp(line, "FILES") == 0) {
        collectingFiles = true;
        firFilesPendingLen = 0;
        return;
    }

    if (strcmp(line, "EVENT boot") == 0) {
        DebugSerial.println("Teensy booted - syncing DSP state");
        updateTeensyWithActivePresetParameters();
        loadFirFilters();
        requestFirFilesRefresh();
        return;
    }

    if (strncmp(line, "PONG ", 5) == 0) {
        static unsigned long lastUptime = 0;
        unsigned long uptime = strtoul(line + 5, nullptr, 10);
        // Uptime going backwards means the Teensy rebooted and we missed its
        // boot event (e.g. it happened while we were rebooting too).
        if (lastUptime > 0 && uptime < lastUptime) {
            DebugSerial.println("Teensy reboot detected - re-syncing DSP state");
            updateTeensyWithActivePresetParameters();
            loadFirFilters();
            requestFirFilesRefresh();
        }
        lastUptime = uptime;
        return;
    }

    DebugSerial.print("Teensy: ");
    DebugSerial.println(line);
}

// --- Setup / loop ---

void initTeensyComm() {
    memset(cmdQueue, 0, sizeof(cmdQueue));
    queueMutex = xSemaphoreCreateMutex();
    firCacheMutex = xSemaphoreCreateMutex();
    // Ask for the file list and FIR gains in case the Teensy was already
    // running when we booted (its boot event would have been missed).
    requestFirFilesRefresh();
    sendToTeensy(CMD_GET_FIR_GAINS, nullptr);
}

void teensyCommLoop() {
    // Drain the outgoing queue without ever blocking: only write a message
    // when it fits in the UART TX buffer in one go. Each entry is copied out
    // under the queue mutex so the UART write happens without holding it.
    for (;;) {
        char msg[TEENSY_MSG_MAX];
        size_t len = 0;

        xSemaphoreTake(queueMutex, portMAX_DELAY);
        while (queueCount > 0 && cmdQueue[queueHead].msg[0] == '\0') {
            // cancelled slot
            queueHead = (queueHead + 1) % QUEUE_SIZE;
            queueCount--;
        }
        if (queueCount > 0) {
            len = strlen(cmdQueue[queueHead].msg);
            if ((size_t)TeensySerial.availableForWrite() >= len) {
                memcpy(msg, cmdQueue[queueHead].msg, len);
                queueHead = (queueHead + 1) % QUEUE_SIZE;
                queueCount--;
            } else {
                len = 0; // TX buffer full; try again next loop()
            }
        }
        xSemaphoreGive(queueMutex);

        if (len == 0) {
            break;
        }
        TeensySerial.write((const uint8_t*)msg, len);
    }

    // Read incoming bytes and assemble lines
    while (TeensySerial.available()) {
        char c = TeensySerial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            rxLine[rxLen] = '\0';
            if (rxOverflow) {
                DebugSerial.println("Teensy RX line too long - dropped");
            } else if (rxLen > 0) {
                handleTeensyLine(rxLine);
            }
            rxLen = 0;
            rxOverflow = false;
        } else if (rxLen < sizeof(rxLine) - 1) {
            rxLine[rxLen++] = c;
        } else {
            rxOverflow = true;
        }
    }

    // Heartbeat ping (reboot detection fallback)
    static unsigned long lastPingAt = 0;
    if (millis() - lastPingAt >= PING_INTERVAL_MS) {
        lastPingAt = millis();
        sendToTeensy(CMD_PING, nullptr);
    }
}
