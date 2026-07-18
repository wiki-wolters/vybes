#ifndef TEENSY_COMMANDS_H
#define TEENSY_COMMANDS_H

// Single source of truth for the serial commands the Teensy accepts over the
// ESP link. fir_filters.ino expands this list to register its handlers with
// the SerialCommandRouter; the host-native test suite expands it to verify
// that every command the ESP can send (teensy_comm.h CMD_*) round-trips to a
// registered handler.
//
// Each entry is X(commandName, handlerFunction).
#define TEENSY_COMMAND_LIST(X) \
  X(setSpeakerGains, handleSetSpeakerGains) \
  X(setMute, handleSetMute) \
  X(setMutePercent, handleSetMutePercent) \
  X(setVolume, handleSetVolume) \
  X(getFiles, handleGetFiles) \
  X(setInputGains, handleSetInputGains) \
  X(setCrossoverFrequency, handleSetCrossoverFrequency) \
  X(setEqEnabled, handleSetEQEnabled) \
  X(setCrossoverEnabled, handleSetCrossoverEnabled) \
  X(setFirEnabled, handleSetFIREnabled) \
  X(loadFirFiles, handleLoadFirFiles) \
  X(setDelayEnabled, handleSetDelayEnabled) \
  X(setFir, handleSetFIR) \
  X(setDelays, handleSetDelays) \
  X(setEq, handleSetEQFilter) \
  X(resetEqFilters, handleResetEQFilters) \
  X(setTone, handleSetTone) \
  X(stopTone, handleStopTone) \
  X(setNoise, handleSetNoise) \
  X(setRta, handleSetRta) \
  X(ping, handlePing)

#endif // TEENSY_COMMANDS_H
