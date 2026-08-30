#ifndef RECORDER_CONTROL_H
#define RECORDER_CONTROL_H

#include <Arduino.h>
#include "OutputStream.h"

// SD recorder/player glue between the command router and the SdRecorder /
// SdWavPlayer objects: the unsolicited status stream and the command
// handlers. Protocol (teensy_protocol.h): status goes to the ESP as
//   REC STATE <sd> <rec> <recFile|-> <recSecs> <play> <playFile|-> <pos> <len>
//   REC ERR <code> <file|->     (nosd, busy, badname, mkdir, full, create,
//                                write, notfound, format, delete)
//   REC WARN <what>             (overrun, stopped firload)
// and the recordings list as "RECFILES <sd>" ... "EOT".

// Drain recorder/player events and send one "REC STATE" line on every state
// change, which includes the once-a-second tick of a running recording's or
// playback's position. Idle, this sends only when the SD card comes or
// goes. Call every loop() pass; setting recStateDirty (SketchState.h) makes
// the next pass send immediately instead of waiting for the 1Hz poll.
void recorderStatusLoop();

// Command handlers (registered through TEENSY_COMMAND_LIST in the sketch)
void handleStartRecording(const String& command, String* args, int argCount, OutputStream& stream);
void handleStopRecording(const String& command, String* args, int argCount, OutputStream& stream);
void handleGetRecordings(const String& command, String* args, int argCount, OutputStream& stream);
void handlePlayRecording(const String& command, String* args, int argCount, OutputStream& stream);
void handleStopPlayback(const String& command, String* args, int argCount, OutputStream& stream);
void handleDeleteRecording(const String& command, String* args, int argCount, OutputStream& stream);

#endif // RECORDER_CONTROL_H
