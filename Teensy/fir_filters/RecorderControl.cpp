#include "RecorderControl.h"

#include <SPI.h>
#include <SD.h>

#include "SketchState.h"
#include "SdRecorder.h" // RECORDINGS_DIR
#include "WavFormat.h"

// The recordings list, one "name bytes seconds" line per file. Sent in reply
// to getRecordings and unsolicited whenever the set changes (a recording
// finished, a file was deleted) so the ESP's cache stays fresh.
static void sendRecordingsList() {
  const bool sd = sdReady();
  Serial1.printf("RECFILES %d\n", sd ? 1 : 0);
  if (sd) {
    File dir = SD.open(RECORDINGS_DIR);
    if (dir && dir.isDirectory()) {
      File f = dir.openNextFile();
      while (f) {
        // Skip dotfiles for the same reason handleGetFiles does (macOS
        // AppleDouble sidecars on removable media)
        if (!f.isDirectory() && f.name()[0] != '.') {
          unsigned long size = (unsigned long)f.size();
          // Seconds assume our own canonical 44-byte header; foreign WAVs
          // dropped into the directory come out approximate, which the
          // player's own parse corrects at play time.
          unsigned long secs = 0;
          if (size > WavFormat::HEADER_BYTES) {
            secs = (size - WavFormat::HEADER_BYTES) /
                   (4UL * (unsigned long)AUDIO_SAMPLE_RATE);
          }
          Serial1.printf("%s %lu %lu\n", f.name(), size, secs);
        }
        f.close();
        f = dir.openNextFile();
      }
    }
    if (dir) dir.close();
  }
  Serial1.print("EOT\n");
}

// What a "REC STATE" line reports, minus the filenames (those only change
// when rec/play flips, which is already part of this). One snapshot replaces
// the five parallel last-* statics the change detection used to juggle.
struct RecStatus {
  bool sd = false;
  bool rec = false;
  bool play = false;
  uint32_t recSec = 0;
  uint32_t playSec = 0;
};

static bool operator==(const RecStatus& a, const RecStatus& b) {
  return a.sd == b.sd && a.rec == b.rec && a.play == b.play &&
         a.recSec == b.recSec && a.playSec == b.playSec;
}

static RecStatus currentRecStatus() {
  RecStatus s;
  s.sd = sdReady();
  s.rec = sdRecorder.isActive();
  s.play = sdPlayer.isActive();
  s.recSec = s.rec ? sdRecorder.seconds() : 0;
  s.playSec = s.play ? sdPlayer.positionSeconds() : 0;
  return s;
}

static void sendRecState(const RecStatus& s) {
  Serial1.printf("REC STATE %d %d %s %lu %d %s %lu %lu\n",
                 s.sd ? 1 : 0, s.rec ? 1 : 0,
                 s.rec && sdRecorder.fileName()[0] ? sdRecorder.fileName() : "-",
                 (unsigned long)s.recSec, s.play ? 1 : 0,
                 s.play && sdPlayer.fileName()[0] ? sdPlayer.fileName() : "-",
                 (unsigned long)s.playSec,
                 (unsigned long)(s.play ? sdPlayer.lengthSeconds() : 0));
}

void recorderStatusLoop() {
  static RecStatus last;
  static unsigned long lastPollMs = 0;

  // Feeds sdReady()'s probe hold-off (runs every loop pass, so the window
  // also covers the card finishing its final writes just after a stop)
  if (sdRecorder.isActive() || sdPlayer.isActive()) {
    sdLastStreamActivityMs = millis();
  }

  if (sdPlayer.consumeFinishedEvent()) recStateDirty = true;
  const char* err = sdRecorder.consumeError();
  if (err != nullptr) {
    // A write failure has already ended the recording (finalized as far as
    // the card allowed)
    Serial1.printf("REC ERR %s %s\n", err,
                   sdRecorder.fileName()[0] ? sdRecorder.fileName() : "-");
    recStateDirty = true;
  }
  if (sdRecorder.consumeOverrunWarning()) {
    Serial1.print("REC WARN overrun\n");
  }

  // At most one status poll per second, unless something marked the state
  // dirty for an immediate send
  if (!recStateDirty && millis() - lastPollMs < 1000) return;
  lastPollMs = millis();

  const RecStatus now = currentRecStatus();
  if (!recStateDirty && now == last) return;

  // A recording that just ended - stopped, failed, or bumped by a FIR load -
  // put a new file on the card
  if (last.rec && !now.rec) sendRecordingsList();

  last = now;
  recStateDirty = false;
  sendRecState(now);
}

// A recording name from the ESP must be a bare filename - no paths, no
// dotfiles. Filenames can't contain spaces (the router splits on them), so
// a valid name always arrives as exactly one argument.
static bool validRecordingName(const String& n) {
  if (n.length() == 0 || n.length() >= 48) return false;
  if (n[0] == '.') return false;
  if (n.indexOf('/') >= 0 || n.indexOf('\\') >= 0) return false;
  return true;
}

void handleStartRecording(const String& command, String* args, int argCount, OutputStream& stream) {
  // Recording and playback both stream the card; one at a time. Stopped
  // first so sdReady()'s media probe never lands on an open read stream.
  if (sdPlayer.isActive()) sdPlayer.stop();
  if (!sdReady()) {
    Serial1.print("REC ERR nosd -\n");
    return;
  }
  const char* err = sdRecorder.start();
  if (err != nullptr) {
    Serial1.printf("REC ERR %s -\n", err);
  }
  recStateDirty = true;
}

void handleStopRecording(const String& command, String* args, int argCount, OutputStream& stream) {
  sdRecorder.stop();
  recStateDirty = true; // recorderStatusLoop sends the fresh list on the rec->idle edge
}

void handleGetRecordings(const String& command, String* args, int argCount, OutputStream& stream) {
  sendRecordingsList();
}

void handlePlayRecording(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount != 1 || !validRecordingName(args[0])) {
    Serial1.print("REC ERR badname -\n");
    return;
  }
  if (sdRecorder.isActive()) {
    Serial1.printf("REC ERR busy %s\n", args[0].c_str());
    return;
  }
  if (!sdReady()) {
    Serial1.print("REC ERR nosd -\n");
    return;
  }
  char path[80];
  snprintf(path, sizeof(path), RECORDINGS_DIR "/%s", args[0].c_str());
  const char* err = nullptr;
  if (!sdPlayer.play(path, &err)) {
    Serial1.printf("REC ERR %s %s\n", err, args[0].c_str());
  }
  recStateDirty = true;
}

void handleStopPlayback(const String& command, String* args, int argCount, OutputStream& stream) {
  sdPlayer.stop();
  recStateDirty = true;
}

void handleDeleteRecording(const String& command, String* args, int argCount, OutputStream& stream) {
  if (argCount != 1 || !validRecordingName(args[0])) {
    Serial1.print("REC ERR badname -\n");
    return;
  }
  if (sdRecorder.isActive()) {
    Serial1.printf("REC ERR busy %s\n", args[0].c_str());
    return;
  }
  if (!sdReady()) {
    Serial1.print("REC ERR nosd -\n");
    return;
  }
  if (sdPlayer.isActive() && strcmp(sdPlayer.fileName(), args[0].c_str()) == 0) {
    sdPlayer.stop();
  }
  char path[80];
  snprintf(path, sizeof(path), RECORDINGS_DIR "/%s", args[0].c_str());
  if (!SD.remove(path)) {
    Serial1.printf("REC ERR delete %s\n", args[0].c_str());
  }
  // The remove's FAT write may still be programming; hold off media probes
  // (sendRecordingsList calls sdReady) so it can't read as a pulled card
  sdLastStreamActivityMs = millis();
  sendRecordingsList();
  recStateDirty = true;
}
