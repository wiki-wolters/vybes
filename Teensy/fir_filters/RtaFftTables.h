#ifndef RTA_FFT_TABLES_H
#define RTA_FFT_TABLES_H

#include <Arduino.h>
#include <stdint.h>

// Flash-resident (PROGMEM) copies of the CMSIS-DSP tables for the RTA's
// 4096-point real FFT - see RtaFftTables.cpp for why. The twiddle tables
// hold float32 bit patterns; RtaFFT4096 casts them for CMSIS. On the
// Teensy 4 PROGMEM is ordinary memory-mapped flash, so no pgm_read_* is
// needed - the arrays just live outside RAM1.
extern const uint16_t rtaBitRevIndexTable2048[3808];
extern const uint32_t rtaTwiddleCoef2048Bits[4096];
extern const uint32_t rtaTwiddleCoefRfft4096Bits[4096];

#endif // RTA_FFT_TABLES_H
