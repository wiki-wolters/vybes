/*
 * Facts about the hardware the WebUI talks to that the maths on this side
 * has to agree with.
 */

// The Teensy audio pipeline runs at 44.1 kHz (AUDIO_SAMPLE_RATE_EXACT in the
// Teensy Audio Library). Every response the UI predicts - bell curves, FIR
// kernels, probe schedules - is a response at this rate.
export const DEVICE_SAMPLE_RATE = 44100;
