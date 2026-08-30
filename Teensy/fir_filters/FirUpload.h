#ifndef FIR_UPLOAD_H
#define FIR_UPLOAD_H

#include <Arduino.h>
#include <stdint.h>
#include "OutputStream.h"

// FIR file upload/delete over the UART link (docs/AUTO_FIR_CONTRACTS.md,
// Slice A): firPutBegin/firPut/firPutEnd/firPutAbort/firDelete. The browser
// streams a file through the ESP in base64-encoded chunks; this module
// writes it to a temp file on the SD card, verifies size+CRC on firPutEnd,
// then atomically replaces the target name.
//
// The state machine (sequence tracking, base64/CRC handling, ERR-reason
// selection, atomicity) is written against FirUploadStorage - a minimal
// stand-in for the bits of the SD/File API this module needs - so it can
// run against an in-memory fake in the host-native test suite, the same
// split CoeffSource/FIRLoader already use for the read side.

// Longest name this module accepts. Mirrors FIR_FILENAME_LEN (ESP
// config.h) / MAX_FILENAME_LEN (SketchState.h) - kept as its own constant
// rather than a shared header so this module stays free of both those
// headers' dependency chains (SketchState.h pulls in the whole audio graph).
#define FIR_UPLOAD_NAME_MAX 63

class FirUploadStorage {
public:
    virtual ~FirUploadStorage() {}

    // SD contention: false while the card is not mounted/ready.
    virtual bool ready() = 0;
    // True while the recorder or player has the card open (SD contention;
    // maps to the "busy" wire reason alongside "another transfer active").
    virtual bool busy() = 0;

    // Create/truncate the upload temp file for writing. False = "sd".
    virtual bool openTemp() = 0;
    // Append to the (already open) temp file. False = "noSpace".
    virtual bool writeTemp(const uint8_t* data, size_t len) = 0;
    // Close the temp file if open; harmless if it isn't.
    virtual void closeTemp() = 0;
    // Remove the temp file if it exists; harmless if it doesn't.
    virtual void removeTemp() = 0;

    virtual bool exists(const char* name) = 0;
    // Remove an arbitrary (non-temp) file. False if it didn't exist or
    // couldn't be removed.
    virtual bool removeFile(const char* name) = 0;
    // Rename the (closed) temp file to name, replacing any existing file of
    // that name. False = "sd".
    virtual bool renameTempTo(const char* name) = 0;

    // Exact tap count for the just-written file (named 'name', 'sizeBytes'
    // long), or 0 if it can't be determined. Mirrors handleGetFiles'
    // listing: size/4 for .bin, header/token counts for .wav/.txt.
    virtual long countTaps(const char* name, uint32_t sizeBytes) = 0;
};

#ifndef VYBES_NATIVE
#include <SD.h> // File

// Real SD-card-backed storage (production).
class SdFirUploadStorage : public FirUploadStorage {
public:
    bool ready() override;
    bool busy() override;
    bool openTemp() override;
    bool writeTemp(const uint8_t* data, size_t len) override;
    void closeTemp() override;
    void removeTemp() override;
    bool exists(const char* name) override;
    bool removeFile(const char* name) override;
    bool renameTempTo(const char* name) override;
    long countTaps(const char* name, uint32_t sizeBytes) override;

private:
    File tempFile;
};
#endif // VYBES_NATIVE

// Swap the storage backend. Production wires up SdFirUploadStorage once at
// startup; the native test suite injects a fake. Never null after either.
void firUploadSetStorage(FirUploadStorage* storage);

// SerialCommandRouter handlers (TeensyCommands.h registers these).
void handleFirPutBegin(const String& command, String* args, int argCount, OutputStream& stream);
void handleFirPut(const String& command, String* args, int argCount, OutputStream& stream);
void handleFirPutEnd(const String& command, String* args, int argCount, OutputStream& stream);
void handleFirPutAbort(const String& command, String* args, int argCount, OutputStream& stream);
void handleFirDelete(const String& command, String* args, int argCount, OutputStream& stream);

#ifdef VYBES_NATIVE
// Test-only: force the session back to idle regardless of what a previous
// test left it in (production never needs this - a real boot starts idle -
// and the real firmware doesn't build it in: every byte of ITCM is spoken
// for, see the native build_src_filter comment in platformio.ini).
void firUploadResetForTest();
#endif

#endif // FIR_UPLOAD_H
