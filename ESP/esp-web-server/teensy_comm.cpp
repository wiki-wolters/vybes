#include "globals.h"
#include "teensy_comm.h"
#include "config.h"

// Outgoing queue. A full preset sync is ~30 commands.
#define QUEUE_SIZE 40
// Incoming line assembly
#define RX_LINE_MAX 160
// Cached SD file list (newline separated)
#define FIR_CACHE_MAX 768
// Heartbeat: detects a Teensy reboot even if its boot event was missed
#define PING_INTERVAL_MS 5000

struct QueuedCommand {
    char msg[TEENSY_MSG_MAX];     // full message incl. trailing newline; "" = cancelled slot
};

static QueuedCommand cmdQueue[QUEUE_SIZE];
static uint8_t queueHead = 0;     // index of next command to send
static uint8_t queueCount = 0;

// RX state
static char rxLine[RX_LINE_MAX];
static size_t rxLen = 0;
static bool rxOverflow = false;

// FIR file list cache, filled asynchronously from "FILES ... EOT" replies
static char firFilesCache[FIR_CACHE_MAX] = {0};
static char firFilesPending[FIR_CACHE_MAX];
static size_t firFilesPendingLen = 0;
static bool collectingFiles = false;

// --- Message building ---

static size_t buildMessage(char* out, size_t outSize, const char* command,
                           const char* p1, const char* p2, const char* p3, const char* p4) {
    size_t offset = strlcpy(out, command, outSize);
    const char* params[4] = {p1, p2, p3, p4};
    for (int i = 0; i < 4; i++) {
        if (!params[i]) continue;
        if (offset < outSize - 1) {
            out[offset++] = ' ';
            out[offset] = '\0';
        }
        offset += strlcpy(out + offset, params[i], outSize - offset);
        if (offset >= outSize) offset = outSize - 1; // strlcpy reports intended length
    }
    if (offset < outSize - 1) {
        out[offset++] = '\n';
        out[offset] = '\0';
    } else {
        DebugSerial.print("Teensy command truncated: ");
        DebugSerial.println(out);
        out[outSize - 2] = '\n';
        out[outSize - 1] = '\0';
        offset = outSize - 1;
    }
    return offset;
}

// --- Coalescing ---

// Extract the first two space-separated tokens of a message.
static void firstTokens(const char* msg, char* t1, size_t s1, char* t2, size_t s2) {
    size_t i = 0, j = 0;
    while (msg[i] && msg[i] != ' ' && msg[i] != '\n' && j < s1 - 1) t1[j++] = msg[i++];
    t1[j] = '\0';
    t2[0] = '\0';
    if (msg[i] == ' ') {
        i++;
        j = 0;
        while (msg[i] && msg[i] != ' ' && msg[i] != '\n' && j < s2 - 1) t2[j++] = msg[i++];
        t2[j] = '\0';
    }
}

// Two messages coalesce when they set the same parameter: same command, and
// for per-slot commands (setEq, setFir) the same slot argument. The newer
// message replaces the older one in place, preserving queue order.
static bool coalesces(const char* a, const char* b) {
    char a1[24], a2[24], b1[24], b2[24];
    firstTokens(a, a1, sizeof(a1), a2, sizeof(a2));
    firstTokens(b, b1, sizeof(b1), b2, sizeof(b2));
    if (strcmp(a1, b1) != 0) return false;
    if (strcmp(a1, CMD_SET_EQ_FILTER) == 0 || strcmp(a1, CMD_SET_FIR) == 0) {
        return strcmp(a2, b2) == 0;
    }
    return true;
}

// A "resetEqFilters N" cancels any queued setEq for a band >= N, so a stale
// pending point can't re-enable a band the reset just disabled.
static void cancelSupersededEqCommands(const char* resetMsg) {
    char t1[24], t2[24];
    firstTokens(resetMsg, t1, sizeof(t1), t2, sizeof(t2));
    int fromIndex = atoi(t2);
    for (uint8_t i = 0; i < queueCount; i++) {
        QueuedCommand& e = cmdQueue[(queueHead + i) % QUEUE_SIZE];
        if (e.msg[0] == '\0') continue;
        char e1[24], e2[24];
        firstTokens(e.msg, e1, sizeof(e1), e2, sizeof(e2));
        if (strcmp(e1, CMD_SET_EQ_FILTER) == 0 && atoi(e2) >= fromIndex) {
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
                  const char* param3, const char* param4) {
    char message[TEENSY_MSG_MAX];
    buildMessage(message, sizeof(message), command, param1, param2, param3, param4);
    if (strcmp(command, CMD_RESET_EQ_FILTERS) == 0) {
        cancelSupersededEqCommands(message);
    }
    return enqueueMessage(message);
}

bool sendToTeensy(const char* command, const String& param1,
                  const String& param2, const String& param3, const String& param4) {
    return sendToTeensy(
        command,
        param1.length() > 0 ? param1.c_str() : nullptr,
        param2.length() > 0 ? param2.c_str() : nullptr,
        param3.length() > 0 ? param3.c_str() : nullptr,
        param4.length() > 0 ? param4.c_str() : nullptr
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

const char* getCachedFirFiles() {
    return firFilesCache;
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
    if (collectingFiles) {
        if (strcmp(line, "EOT") == 0) {
            memcpy(firFilesCache, firFilesPending, firFilesPendingLen);
            firFilesCache[firFilesPendingLen] = '\0';
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
    // Ask for the file list in case the Teensy was already running when we
    // booted (its boot event would have been missed).
    requestFirFilesRefresh();
}

void teensyCommLoop() {
    // Drain the outgoing queue without ever blocking: only write a message
    // when it fits in the UART TX buffer in one go.
    while (queueCount > 0) {
        QueuedCommand& e = cmdQueue[queueHead];
        if (e.msg[0] == '\0') { // cancelled slot
            queueHead = (queueHead + 1) % QUEUE_SIZE;
            queueCount--;
            continue;
        }
        size_t len = strlen(e.msg);
        if ((size_t)TeensySerial.availableForWrite() < len) {
            break; // TX buffer full; try again next loop()
        }
        TeensySerial.write((const uint8_t*)e.msg, len);
        queueHead = (queueHead + 1) % QUEUE_SIZE;
        queueCount--;
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
