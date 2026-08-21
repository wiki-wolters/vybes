#ifndef API_RECORDER_H
#define API_RECORDER_H

#include <PsychicHttp.h>

// SD recorder / player REST API. State changes are asynchronous: these
// handlers queue the command over the Teensy link and reply immediately;
// the resulting state reaches clients as recorderState websocket messages.
//
//   GET    /recorder             full state + recordings list (cached)
//   POST   /recorder/record/start
//   POST   /recorder/record/stop
//   POST   /recorder/play?name=rec-001.wav
//   POST   /recorder/play/stop
//   DELETE /recorder/file?name=rec-001.wav
//
// Mutating anything the Teensy would have to read the SD card for while a
// recording runs (preset switches, FIR edits, restores) is refused with 409
// at those endpoints; play and delete are refused here for the same reason.

esp_err_t handleGetRecorder(PsychicRequest *request);
esp_err_t handlePostRecordStart(PsychicRequest *request);
esp_err_t handlePostRecordStop(PsychicRequest *request);
esp_err_t handlePostRecorderPlay(PsychicRequest *request);
esp_err_t handlePostRecorderPlayStop(PsychicRequest *request);
esp_err_t handleDeleteRecording(PsychicRequest *request);

#endif // API_RECORDER_H
