#ifndef FIR_FILES_H
#define FIR_FILES_H

// Loads the per-output FIR files named in state.outputs[].firFile from the
// SD card into the firFilter[] objects: the three-pass size/carve/fill
// orchestration and the static arena it fills (see loadFirFiles in
// FirFiles.cpp). Loop() context only - it blocks on SD reads.

// FIR taps shared across all outputs (FIR_TAP_POOL on the ESP). Loads that
// would push the total over the pool are rejected with an error the ESP can
// relay. Fast convolution costs 16 bytes/tap (2 x partitions x 256 floats),
// so a full pool is ~192KB - reserved once, statically, as firArena rather
// than fought for on the heap at every load. The pool is what that fixed
// block holds, not a guess at what the heap can spare. It is also why
// loadFirFiles streams coefficients into the engine instead of reading the
// file into an array first: a whole-file copy would need another 4 bytes/tap
// of heap on top, and an exact-fit set (3072+3072+6144) has none to give.
// The direct engine runs out of CPU long before it runs out of pool.
#define FIR_TAP_POOL 12288

// Size every named file, carve the arena into a slice per output, then
// stream the coefficients in. Ends by re-running applyDelays() (FIR
// latencies may have changed). Failures are per-channel and reported to the
// ESP as FIRERR lines; the other channels still load.
void loadFirFiles();

#endif // FIR_FILES_H
