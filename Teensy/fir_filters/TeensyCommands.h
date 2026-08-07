#ifndef TEENSY_COMMANDS_H
#define TEENSY_COMMANDS_H

// Single source of truth for the serial commands the Teensy accepts over the
// ESP link. fir_filters.ino expands this list to register its handlers with
// the SerialCommandRouter; the host-native test suite expands it to verify
// that every command the ESP can send (teensy_protocol.h CMD_*) round-trips
// to a registered handler.
//
// Each entry is X(commandName, handlerFunction).
#define TEENSY_COMMAND_LIST(X) \
  X(setOutputGain, handleSetOutputGain) \
  X(setOutputMute, handleSetOutputMute) \
  X(setOutputInvert, handleSetOutputInvert) \
  X(setOutputSource, handleSetOutputSource) \
  X(setOutputDelay, handleSetOutputDelay) \
  X(setOutputHp, handleSetOutputHp) \
  X(setOutputLp, handleSetOutputLp) \
  X(setOutputEq, handleSetOutputEq) \
  X(resetOutputEq, handleResetOutputEq) \
  X(setInputEq, handleSetInputEq) \
  X(resetInputEq, handleResetInputEq) \
  X(setInputEqEnabled, handleSetInputEqEnabled) \
  X(setFir, handleSetFIR) \
  X(setFirEnabled, handleSetFIREnabled) \
  X(loadFirFiles, handleLoadFirFiles) \
  X(getFiles, handleGetFiles) \
  X(setDelaysEnabled, handleSetDelaysEnabled) \
  X(setSpeakerGains, handleSetSpeakerGains) \
  X(setInputGains, handleSetInputGains) \
  X(setVolume, handleSetVolume) \
  X(setTone, handleSetTone) \
  X(stopTone, handleStopTone) \
  X(setNoise, handleSetNoise) \
  X(setRta, handleSetRta) \
  X(setCompEnabled, handleSetCompEnabled) \
  X(setCompXover, handleSetCompXover) \
  X(setCompBand, handleSetCompBand) \
  X(setCompBandBypass, handleSetCompBandBypass) \
  X(setCompSolo, handleSetCompSolo) \
  X(setCompStrength, handleSetCompStrength) \
  X(setCompVoicePriority, handleSetCompVoicePriority) \
  X(setGrm, handleSetGrm) \
  X(startDelayProbe, handleStartDelayProbe) \
  X(stopDelayProbe, handleStopDelayProbe) \
  X(soloOutput, handleSoloOutput) \
  X(setMute, handleSetMute) \
  X(setMutePercent, handleSetMutePercent) \
  X(ping, handlePing)

#endif // TEENSY_COMMANDS_H
