#include "FirUpload.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "teensy_protocol.h" // FIR_UPLOAD_*, FIR_PUT_*, crc32*/base64* helpers

#ifndef VYBES_NATIVE
#include <SPI.h>
#include <SD.h>
#include "SketchState.h" // sdReady(), sdRecorder, sdPlayer
#include "FIRLoader.h"
#endif

// --- Session state machine (storage-agnostic; see FirUpload.h) ---

enum class FirUploadPhase { Idle, Writing };

struct FirUploadSession {
    FirUploadPhase phase = FirUploadPhase::Idle;
    char name[FIR_UPLOAD_NAME_MAX + 1] = "";
    uint32_t claimedSize = 0;
    uint32_t claimedCrc = 0;
    uint32_t bytesWritten = 0;
    uint32_t crc = 0;
    int32_t nextSeq = 0;
};

static FirUploadSession session;
static FirUploadStorage* storage = nullptr;

static void resetSession() {
    session = FirUploadSession();
}

static void replyErr(OutputStream& out, const char* reason) {
    out.printf("FIRPUT ERR %s\n", reason);
}

// Any ERR aborts the transfer: leave the SD exactly as it was (temp file
// removed) and drop back to idle so a fresh Begin can start clean.
static void abortSession(OutputStream& out, const char* reason) {
    if (storage) {
        storage->closeTemp();
        storage->removeTemp();
    }
    resetSession();
    replyErr(out, reason);
}

// A name must survive the wire intact (already guaranteed - filenames can't
// contain spaces, see the ESP's isValidFirFilename) and stay on the SD
// root: no path separators or leading dot, mirroring the recorder's own
// filename validator (RecorderControl.cpp) since these files share a card.
static bool validName(const String& s) {
    size_t len = s.length();
    if (len == 0 || len > FIR_UPLOAD_NAME_MAX) return false;
    if (s[0] == '.') return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = s[i];
        if (c <= ' ' || c == 0x7F || c == '/' || c == '\\') return false;
    }
    return true;
}

static bool endsWithCI(const char* s, const char* suffix) {
    size_t sl = strlen(s), pl = strlen(suffix);
    if (pl > sl) return false;
    for (size_t i = 0; i < pl; i++) {
        if (tolower((unsigned char)s[sl - pl + i]) != tolower((unsigned char)suffix[i])) return false;
    }
    return true;
}

static bool isBinName(const char* name) {
    return endsWithCI(name, ".bin");
}

// Strict decimal parse (unlike String::toInt(), which silently returns 0 for
// garbage) - callers need to tell "seq 0" from "seq not-a-number".
static bool parseNonNegativeLong(const char* s, long* out) {
    if (s == nullptr || *s == '\0') return false;
    char* end = nullptr;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 0) return false;
    *out = v;
    return true;
}

void firUploadSetStorage(FirUploadStorage* s) {
    storage = s;
}

#ifdef VYBES_NATIVE
void firUploadResetForTest() {
    if (storage) {
        storage->closeTemp();
        storage->removeTemp();
    }
    resetSession();
}
#endif

// "firPutBegin <name> <size> <crc32>" -> FIRPUT BEGIN <name> | FIRPUT ERR <reason>
void handleFirPutBegin(const String& command, String* args, int argCount, OutputStream& stream) {
    if (argCount != 3) {
        replyErr(stream, "badName");
        return;
    }
    if (session.phase != FirUploadPhase::Idle) {
        replyErr(stream, "busy");
        return;
    }
    if (!validName(args[0])) {
        replyErr(stream, "badName");
        return;
    }
    char nameBuf[FIR_UPLOAD_NAME_MAX + 1];
    teensyProtocolStrlcpy(nameBuf, args[0].c_str(), sizeof(nameBuf));

    long sizeVal = 0;
    if (!parseNonNegativeLong(args[1].c_str(), &sizeVal) ||
        sizeVal < (long)FIR_UPLOAD_MIN_SIZE || sizeVal > (long)FIR_UPLOAD_MAX_SIZE) {
        replyErr(stream, "badSize");
        return;
    }
    // A .bin is raw float32 taps: the byte count must be a whole number of them.
    if (isBinName(nameBuf) && (sizeVal % 4) != 0) {
        replyErr(stream, "badSize");
        return;
    }

    uint32_t crcVal = 0;
    if (!crc32FromHex(args[2].c_str(), &crcVal)) {
        replyErr(stream, "crc");
        return;
    }

    if (!storage) {
        replyErr(stream, "sd");
        return;
    }
    // Recording/playback or another transfer holding the card.
    if (storage->busy()) {
        replyErr(stream, "busy");
        return;
    }
    if (!storage->ready()) {
        replyErr(stream, "sd");
        return;
    }
    if (!storage->openTemp()) {
        replyErr(stream, "sd");
        return;
    }

    resetSession();
    teensyProtocolStrlcpy(session.name, nameBuf, sizeof(session.name));
    session.claimedSize = (uint32_t)sizeVal;
    session.claimedCrc = crcVal;
    session.crc = crc32Init();
    session.phase = FirUploadPhase::Writing;

    stream.printf("FIRPUT BEGIN %s\n", session.name);
}

// "firPut <seq> <base64>" -> FIRPUT ACK <seq> (every FIR_PUT_ACK_STRIDE'th
// line and the final line) | FIRPUT ERR <reason> (aborts the transfer)
void handleFirPut(const String& command, String* args, int argCount, OutputStream& stream) {
    if (session.phase != FirUploadPhase::Writing) {
        replyErr(stream, "state");
        return;
    }
    if (argCount != 2) {
        abortSession(stream, "badB64");
        return;
    }

    long seq = 0;
    if (!parseNonNegativeLong(args[0].c_str(), &seq)) {
        abortSession(stream, "badSeq");
        return;
    }
    if (seq != session.nextSeq) {
        abortSession(stream, "badSeq"); // gap or repeat
        return;
    }

    const String& b64 = args[1];
    if (b64.length() == 0 || b64.length() > FIR_PUT_B64_MAX) {
        abortSession(stream, "badB64"); // includes an oversize line
        return;
    }
    uint8_t raw[FIR_PUT_CHUNK_BYTES];
    long rawLen = base64Decode(b64.c_str(), b64.length(), raw, sizeof(raw));
    if (rawLen <= 0) {
        abortSession(stream, "badB64");
        return;
    }
    if (session.bytesWritten + (uint32_t)rawLen > session.claimedSize) {
        abortSession(stream, "badSize"); // more bytes than firPutBegin claimed
        return;
    }
    if (!storage->writeTemp(raw, (size_t)rawLen)) {
        abortSession(stream, "noSpace");
        return;
    }

    session.crc = crc32Update(session.crc, raw, (size_t)rawLen);
    session.bytesWritten += (uint32_t)rawLen;
    session.nextSeq++;

    const bool isFinal = (session.bytesWritten == session.claimedSize);
    const bool strideAck = ((seq % FIR_PUT_ACK_STRIDE) == (FIR_PUT_ACK_STRIDE - 1));
    if (isFinal || strideAck) {
        stream.printf("FIRPUT ACK %ld\n", (long)seq);
    }
}

// "firPutEnd" -> FIRPUT OK <name> <size> <taps> | FIRPUT ERR <reason>
void handleFirPutEnd(const String& command, String* args, int argCount, OutputStream& stream) {
    if (session.phase != FirUploadPhase::Writing) {
        replyErr(stream, "state");
        return;
    }
    if (session.bytesWritten != session.claimedSize) {
        abortSession(stream, "badSize");
        return;
    }
    if (crc32Finish(session.crc) != session.claimedCrc) {
        abortSession(stream, "crc");
        return;
    }

    storage->closeTemp();

    // Atomic replace: drop any existing target, then rename the verified
    // temp file into place. A rename failure leaves the SD as it was (temp
    // file removed), not a half-renamed or duplicated file.
    if (storage->exists(session.name)) {
        storage->removeFile(session.name);
    }
    if (!storage->renameTempTo(session.name)) {
        storage->removeTemp();
        resetSession();
        replyErr(stream, "sd");
        return;
    }

    long taps;
    if (isBinName(session.name)) {
        taps = (long)(session.claimedSize / 4); // exact: raw float32 taps
    } else {
        taps = storage->countTaps(session.name, session.claimedSize);
        if (taps < 0) taps = 0;
    }

    char nameCopy[FIR_UPLOAD_NAME_MAX + 1];
    teensyProtocolStrlcpy(nameCopy, session.name, sizeof(nameCopy));
    uint32_t sizeCopy = session.claimedSize;
    resetSession();

    stream.printf("FIRPUT OK %s %lu %lu\n", nameCopy, (unsigned long)sizeCopy, (unsigned long)taps);
}

// "firPutAbort" -> FIRPUT STOP (always; idempotent even if nothing was
// running, so the ESP's own timeout/cleanup path never has to guess).
void handleFirPutAbort(const String& command, String* args, int argCount, OutputStream& stream) {
    if (session.phase == FirUploadPhase::Writing) {
        if (storage) {
            storage->closeTemp();
            storage->removeTemp();
        }
        resetSession();
    }
    stream.print("FIRPUT STOP\n");
}

// "firDelete <name>" -> FIRDEL OK <name> | FIRDEL ERR <reason>
void handleFirDelete(const String& command, String* args, int argCount, OutputStream& stream) {
    if (argCount != 1 || !validName(args[0])) {
        stream.print("FIRDEL ERR badName\n");
        return;
    }
    // A live upload holds the card - reject rather than race its temp file.
    if (session.phase != FirUploadPhase::Idle) {
        stream.print("FIRDEL ERR busy\n");
        return;
    }
    if (!storage) {
        stream.print("FIRDEL ERR sd\n");
        return;
    }
    if (storage->busy()) {
        stream.print("FIRDEL ERR busy\n");
        return;
    }
    if (!storage->ready()) {
        stream.print("FIRDEL ERR sd\n");
        return;
    }

    char nameBuf[FIR_UPLOAD_NAME_MAX + 1];
    teensyProtocolStrlcpy(nameBuf, args[0].c_str(), sizeof(nameBuf));
    if (!storage->exists(nameBuf)) {
        stream.print("FIRDEL ERR notfound\n");
        return;
    }
    if (!storage->removeFile(nameBuf)) {
        stream.print("FIRDEL ERR sd\n");
        return;
    }
    stream.printf("FIRDEL OK %s\n", nameBuf);
}

#ifndef VYBES_NATIVE

bool SdFirUploadStorage::ready() {
    return sdReady();
}

bool SdFirUploadStorage::busy() {
    return sdRecorder.isActive() || sdPlayer.isActive();
}

bool SdFirUploadStorage::openTemp() {
    SD.remove(FIR_UPLOAD_TMP_NAME); // guarantee a clean slate regardless of
                                    // FILE_WRITE_BEGIN's truncate semantics
    tempFile = SD.open(FIR_UPLOAD_TMP_NAME, FILE_WRITE_BEGIN);
    return (bool)tempFile;
}

bool SdFirUploadStorage::writeTemp(const uint8_t* data, size_t len) {
    if (!tempFile) return false;
    return tempFile.write(data, len) == len;
}

void SdFirUploadStorage::closeTemp() {
    if (tempFile) tempFile.close();
}

void SdFirUploadStorage::removeTemp() {
    SD.remove(FIR_UPLOAD_TMP_NAME);
}

bool SdFirUploadStorage::exists(const char* name) {
    return SD.exists(name);
}

bool SdFirUploadStorage::removeFile(const char* name) {
    return SD.remove(name);
}

bool SdFirUploadStorage::renameTempTo(const char* name) {
    return SD.rename(FIR_UPLOAD_TMP_NAME, name);
}

long SdFirUploadStorage::countTaps(const char* name, uint32_t sizeBytes) {
    (void)sizeBytes;
    bool isWav = endsWithCI(name, ".wav");
    bool isTxt = endsWithCI(name, ".txt");
    if (!isWav && !isTxt) return 0;
    File file = SD.open(name);
    if (!file) return 0;
    long taps = isWav ? FIRLoader::countWavTaps(file) : FIRLoader::countTxtTaps(file);
    file.close();
    return taps;
}

// Wires the real SD-backed storage in before setup() runs (C++ static
// initialization order), so production never needs an explicit call from
// the sketch - fir_filters.ino only has to include FirUpload.h (via
// TeensyCommands.h) to register the command handlers.
namespace {
SdFirUploadStorage gDefaultFirUploadStorage;
struct FirUploadStorageInstaller {
    FirUploadStorageInstaller() { firUploadSetStorage(&gDefaultFirUploadStorage); }
} gFirUploadStorageInstaller;
} // namespace

#endif // VYBES_NATIVE
