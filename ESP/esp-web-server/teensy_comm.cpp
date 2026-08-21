#include "globals.h"
#include "teensy_comm.h"
#include "config.h"
#include "websocket.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Outgoing queue. A full V1 preset sync is ~190 commands worst case
// (8 outputs x up to 19 commands each, plus input EQ, dynamics and globals).
#define QUEUE_SIZE 220
// Incoming line assembly. Sized for the longest line the Teensy sends: a
// 121-band RTA frame ("RTA " + 242 hex chars = 246 chars).
#define RX_LINE_MAX 300
// Cached SD file list (newline separated "name size" lines; WAV and TXT
// lines from newer Teensy firmware carry the exact tap count:
// "name size taps")
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

// Recordings list cache, same scheme, from "RECFILES <sd> ... EOT" replies.
// Guarded by firCacheMutex like everything else the RX path caches.
#define REC_CACHE_MAX 1024
static char recFilesCache[REC_CACHE_MAX] = {0};
static char recFilesPending[REC_CACHE_MAX];
static size_t recFilesPendingLen = 0;
static bool collectingRecordings = false;
static bool recSdPresent = false;
static bool recSdPending = false;

// Mirror of the Teensy's last "REC STATE" line; recorderState.recording is
// what locks preset switching. Guarded by firCacheMutex.
static RecorderState recorderState;

// Per-output result of the last FIR load, from the Teensy's "FIRERR ch code
// file" lines. A failed load used to be visible only on the Teensy's debug
// console, so the UI happily showed a channel as FIR-corrected while it was
// running with no filter at all. Cleared when a fresh load is requested.
struct FirLoadError {
    char code[12];
    char file[FIR_FILENAME_LEN + 1];
};
static FirLoadError firLoadErrors[NUM_OUTPUTS] = {};

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

// Ordered barriers: commands whose meaning is their POSITION in the stream,
// not the value they carry. Coalescing rewrites an existing entry in place
// and keeps its old slot, which is right for a parameter ("the latest value
// wins, wherever it sits") and completely wrong for these:
//   setConfigHold - the closing 0 would overwrite the opening 1 at the front
//                   of the sync, collapsing the bracket into a single release
//                   delivered BEFORE any config, unmuting the outputs for the
//                   whole sync - the exact thing the hold exists to prevent.
//   loadFirFiles  - would hop backwards ahead of the setFir commands naming
//                   the files it is supposed to load.
static bool isOrderedBarrier(const char* command) {
    return strcmp(command, CMD_SET_CONFIG_HOLD) == 0 ||
           strcmp(command, CMD_LOAD_FIR_FILES) == 0;
}

// Two messages coalesce when they set the same parameter: same command and
// same identifying arguments. The newer message replaces the older one in
// place, preserving queue order.
static bool coalesces(const char* a, const char* b) {
    char a1[24], a2[24], a3[24], b1[24], b2[24], b3[24];
    firstTokens(a, a1, sizeof(a1), a2, sizeof(a2), a3, sizeof(a3));
    firstTokens(b, b1, sizeof(b1), b2, sizeof(b2), b3, sizeof(b3));
    if (strcmp(a1, b1) != 0) return false;
    if (isOrderedBarrier(a1)) return false;
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

// Look up a file's cache line and return the numeric field at fieldIndex
// (0 = size, 1 = taps), or -1 if the file or the field is absent. A line is
// "name" (old firmware), "name size", or "name size taps" (WAV and TXT
// lines from newer firmware); filenames can't contain spaces (enforced on
// upload), so the first token is always the name.
static long getCachedFirFileField(const char* name, int fieldIndex) {
    size_t nameLen = strlen(name);
    if (nameLen == 0) return -1;

    long value = -1;
    xSemaphoreTake(firCacheMutex, portMAX_DELAY);
    const char* line = firFilesCache;
    while (*line != '\0') {
        const char* end = strchr(line, '\n');
        size_t lineLen = end ? (size_t)(end - line) : strlen(line);
        if (lineLen > nameLen && strncmp(line, name, nameLen) == 0 && line[nameLen] == ' ') {
            // Walk to the requested field (strtol stops at the next space,
            // so a trailing field never corrupts an earlier one)
            const char* field = line + nameLen + 1;
            for (int i = 0; i < fieldIndex && field != nullptr; i++) {
                field = strchr(field, ' ');
                if (field != nullptr) field++;
            }
            // The strchr walk can run past this line's newline into the
            // next entry - a field found there doesn't exist on this line
            if (field != nullptr && (size_t)(field - line) < lineLen) {
                value = strtol(field, nullptr, 10);
                if (value < 0) value = -1;
            }
            break;
        }
        if (lineLen == nameLen && strncmp(line, name, nameLen) == 0) {
            break; // listed without a size: known file, unknown size
        }
        if (!end) break;
        line = end + 1;
    }
    xSemaphoreGive(firCacheMutex);
    return value;
}

long getCachedFirFileSize(const char* name) {
    return getCachedFirFileField(name, 0);
}

long getCachedFirFileTaps(const char* name) {
    return getCachedFirFileField(name, 1);
}

void requestFirFilesRefresh() {
    sendToTeensy(CMD_GET_FILES, nullptr);
}

// --- SD recorder / player state ---

void getRecorderState(RecorderState& out) {
    xSemaphoreTake(firCacheMutex, portMAX_DELAY);
    out = recorderState;
    xSemaphoreGive(firCacheMutex);
}

bool isRecordingActive() {
    xSemaphoreTake(firCacheMutex, portMAX_DELAY);
    const bool active = recorderState.recording;
    xSemaphoreGive(firCacheMutex);
    return active;
}

size_t copyCachedRecordings(char* dst, size_t dstSize) {
    xSemaphoreTake(firCacheMutex, portMAX_DELAY);
    size_t len = strlcpy(dst, recFilesCache, dstSize);
    xSemaphoreGive(firCacheMutex);
    return len;
}

bool recordingsSdPresent() {
    xSemaphoreTake(firCacheMutex, portMAX_DELAY);
    const bool present = recSdPresent;
    xSemaphoreGive(firCacheMutex);
    return present;
}

void requestRecordingsRefresh() {
    sendToTeensy(CMD_GET_RECORDINGS, nullptr);
}

// The Teensy rebooted: whatever was recording or playing died with it. Clear
// the mirrored state (and with it the preset-switch lock) and tell the UI.
static void resetRecorderStateAfterReboot() {
    xSemaphoreTake(firCacheMutex, portMAX_DELAY);
    const bool wasActive = recorderState.recording || recorderState.playing;
    recorderState = RecorderState();
    const RecorderState copy = recorderState;
    xSemaphoreGive(firCacheMutex);
    if (wasActive) {
        broadcastRecorderState(copy);
    }
    requestRecordingsRefresh();
}

// --- FIR load errors ---

void clearFirLoadErrors() {
    xSemaphoreTake(firCacheMutex, portMAX_DELAY);
    memset(firLoadErrors, 0, sizeof(firLoadErrors));
    xSemaphoreGive(firCacheMutex);
}

bool getFirLoadError(int output, char* code, size_t codeSize,
                     char* file, size_t fileSize) {
    if (output < 0 || output >= NUM_OUTPUTS) return false;
    xSemaphoreTake(firCacheMutex, portMAX_DELAY);
    const bool present = firLoadErrors[output].code[0] != '\0';
    if (present) {
        strlcpy(code, firLoadErrors[output].code, codeSize);
        strlcpy(file, firLoadErrors[output].file, fileSize);
    }
    xSemaphoreGive(firCacheMutex);
    return present;
}

// --- RX line handling ---

// Handle one complete line from the Teensy. The Teensy sends:
//   "EVENT boot"        on startup (triggers a full state re-sync)
//   "PONG <uptimeMs>"   in reply to ping (reboot detection fallback)
//   "FILES" ... "EOT"   the SD file list, one "name size [taps]" line per file
// Anything else is forwarded to the debug console.
// Last uptime reported by the Teensy, for reboot detection. File-scope so
// the boot-event path can re-arm it and suppress a duplicate sync.
static unsigned long teensyLastUptime = 0;

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

    // "FIRERR <ch> <code> <file>": a channel's FIR filter did not load. Record
    // it and tell the UI immediately - silently running an uncorrected channel
    // is the worst possible failure mode for a room-correction box.
    if (strncmp(line, "FIRERR ", 7) == 0) {
        char* end = nullptr;
        long ch = strtol(line + 7, &end, 10);
        if (end != nullptr && *end == ' ' && ch >= 0 && ch < NUM_OUTPUTS) {
            char code[12] = {0};
            const char* codeStart = end + 1;
            const char* codeEnd = strchr(codeStart, ' ');
            if (codeEnd != nullptr) {
                size_t codeLen = (size_t)(codeEnd - codeStart);
                if (codeLen >= sizeof(code)) codeLen = sizeof(code) - 1;
                memcpy(code, codeStart, codeLen);
                const char* file = codeEnd + 1;

                xSemaphoreTake(firCacheMutex, portMAX_DELAY);
                strlcpy(firLoadErrors[ch].code, code, sizeof(firLoadErrors[ch].code));
                strlcpy(firLoadErrors[ch].file, file, sizeof(firLoadErrors[ch].file));
                xSemaphoreGive(firCacheMutex);

                DebugSerial.printf("FIR load failed on output %ld (%s): %s\n", ch, code, file);
                // A load only ever concerns the active preset, and the UI
                // filters live messages by preset name.
                broadcastFirLoadError(
                    current_config.presets[current_config.active_preset_index].name,
                    (int)ch, code, file);
            }
        }
        return;
    }

    // "REC ..." recorder/player lines (state, errors, warnings) - see the
    // recorder section of teensy_protocol.h.
    if (strncmp(line, "REC ", 4) == 0) {
        const char* rest = line + 4;
        if (strncmp(rest, "STATE ", 6) == 0) {
            int sd = 0, rec = 0, play = 0;
            char recFile[48], playFile[48];
            unsigned long recSecs = 0, playSecs = 0, playLen = 0;
            if (sscanf(rest + 6, "%d %d %47s %lu %d %47s %lu %lu",
                       &sd, &rec, recFile, &recSecs, &play, playFile,
                       &playSecs, &playLen) == 8) {
                xSemaphoreTake(firCacheMutex, portMAX_DELAY);
                recorderState.sdPresent = sd == 1;
                recorderState.recording = rec == 1;
                strlcpy(recorderState.recordFile,
                        strcmp(recFile, "-") == 0 ? "" : recFile,
                        sizeof(recorderState.recordFile));
                recorderState.recordSeconds = recSecs;
                recorderState.playing = play == 1;
                strlcpy(recorderState.playFile,
                        strcmp(playFile, "-") == 0 ? "" : playFile,
                        sizeof(recorderState.playFile));
                recorderState.playSeconds = playSecs;
                recorderState.playLength = playLen;
                const RecorderState copy = recorderState;
                xSemaphoreGive(firCacheMutex);
                broadcastRecorderState(copy);
            }
            return;
        }
        if (strncmp(rest, "ERR ", 4) == 0) {
            char code[16] = {0};
            const char* file = "";
            const char* space = strchr(rest + 4, ' ');
            size_t codeLen = space ? (size_t)(space - (rest + 4)) : strlen(rest + 4);
            if (codeLen >= sizeof(code)) codeLen = sizeof(code) - 1;
            memcpy(code, rest + 4, codeLen);
            if (space != nullptr && strcmp(space + 1, "-") != 0) file = space + 1;
            DebugSerial.printf("Recorder error: %s %s\n", code, file);
            broadcastRecorderError(code, file);
            return;
        }
        if (strncmp(rest, "WARN ", 5) == 0) {
            DebugSerial.printf("Recorder warning: %s\n", rest + 5);
            broadcastRecorderWarning(rest + 5);
            return;
        }
        // Unknown REC line: fall through to the debug log below
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

    if (collectingRecordings) {
        if (strcmp(line, "EOT") == 0) {
            xSemaphoreTake(firCacheMutex, portMAX_DELAY);
            // Only announce actual changes: every GET /recorder triggers a
            // refresh, and announcing an identical list would make clients
            // refetch (and so refresh) forever.
            const bool changed = recSdPresent != recSdPending ||
                                 strlen(recFilesCache) != recFilesPendingLen ||
                                 memcmp(recFilesCache, recFilesPending, recFilesPendingLen) != 0;
            memcpy(recFilesCache, recFilesPending, recFilesPendingLen);
            recFilesCache[recFilesPendingLen] = '\0';
            recSdPresent = recSdPending;
            // The list reply is also the freshest word on card presence
            recorderState.sdPresent = recSdPending;
            xSemaphoreGive(firCacheMutex);
            collectingRecordings = false;
            if (changed) {
                broadcastRecordingsChanged();
                DebugSerial.println("Recordings list updated");
            }
        } else {
            size_t len = strlen(line);
            if (recFilesPendingLen + len + 1 < sizeof(recFilesPending)) {
                memcpy(recFilesPending + recFilesPendingLen, line, len);
                recFilesPendingLen += len;
                recFilesPending[recFilesPendingLen++] = '\n';
            }
        }
        return;
    }

    if (strcmp(line, "FILES") == 0) {
        collectingFiles = true;
        firFilesPendingLen = 0;
        return;
    }

    if (strncmp(line, "RECFILES", 8) == 0 && (line[8] == '\0' || line[8] == ' ')) {
        collectingRecordings = true;
        recFilesPendingLen = 0;
        recSdPending = line[8] == ' ' && line[9] == '1';
        return;
    }

    if (strcmp(line, "EVENT boot") == 0) {
        DebugSerial.println("Teensy booted - syncing DSP state");
        // The PONG path below also spots this reboot (uptime goes backwards)
        // and would queue a SECOND full sync on top of this one. A sync is
        // ~195 commands against a 220-slot queue, so two of them overflow it
        // and the surplus is dropped - leaving the DSP half-configured.
        // Re-arm the baseline so only this sync runs.
        teensyLastUptime = 0;
        updateTeensyWithActivePresetParameters();
        requestFirFilesRefresh();
        resetRecorderStateAfterReboot();
        return;
    }

    if (strncmp(line, "PONG ", 5) == 0) {
        unsigned long& lastUptime = teensyLastUptime;
        unsigned long uptime = strtoul(line + 5, nullptr, 10);
        // Uptime going backwards means the Teensy rebooted and we missed its
        // boot event (e.g. it happened while we were rebooting too).
        if (lastUptime > 0 && uptime < lastUptime) {
            DebugSerial.println("Teensy reboot detected - re-syncing DSP state");
            updateTeensyWithActivePresetParameters();
            requestFirFilesRefresh();
            resetRecorderStateAfterReboot();
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
    // Ask for the file lists in case the Teensy was already running when we
    // booted (its boot event would have been missed).
    requestFirFilesRefresh();
    requestRecordingsRefresh();
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
