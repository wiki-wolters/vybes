#include "TelemetryStreams.h"

#include <Arduino.h>
#include <math.h>

#include "SketchState.h"
#include "KeepaliveStream.h"
#include "RtaFFT4096.h"
#include "MultibandCompressor.h" // inputComp API + COMP_NUM_BANDS
#include "PeakMeter.h"

static const char HEX_DIGITS[] = "0123456789abcdef";

// --- RTA (real-time analyzer) streaming ---
// Bands are 1/12-octave in the base-10 sense (centers 10^(k/40)),
// 20Hz-20kHz. The web UI (WebUI/src/rta.js) uses the same definition and
// infers the resolution from the frame's band count, so older 31-band
// firmware and this 121-band version both decode.
#define RTA_NUM_BANDS 121
#define RTA_BANDS_PER_DECADE 40
#define RTA_K_LO 52 // 10^(52/40) = 20Hz
#define RTA_FRAME_INTERVAL_MS 100
#define RTA_KEEPALIVE_TIMEOUT_MS 7000
static KeepaliveStream rtaStream(RTA_FRAME_INTERVAL_MS, RTA_KEEPALIVE_TIMEOUT_MS);
static float RTA_BAND_CENTERS[RTA_NUM_BANDS]; // filled by telemetryBegin()

void telemetryBegin() {
  for (int b = 0; b < RTA_NUM_BANDS; b++) {
    RTA_BAND_CENTERS[b] = powf(10.0f, (float)(RTA_K_LO + b) / RTA_BANDS_PER_DECADE);
  }
}

bool rtaStreaming() {
  return rtaStream.enabled();
}

void setRtaEnabled(bool enabled) {
  if (rtaStream.setEnabled(enabled, millis())) {
    Serial.println(enabled ? "RTA started" : "RTA stopped");
    updateRtaSource();
  }
}

// Sum FFT power over [lo,hi) Hz. Edge bins contribute proportionally to
// their overlap with the band, so bands narrower than one ~10.8Hz bin still
// get a sensible share instead of double-counting or reading zero.
static float rtaBandPower(float lo, float hi) {
  const float binWidth = RtaFFT4096::binWidthHz();
  int first = (int)roundf(lo / binWidth);
  int last = (int)roundf(hi / binWidth);
  if (first < 1) first = 1; // skip the DC bin
  if (last > RtaFFT4096::NUM_BINS - 1) last = RtaFFT4096::NUM_BINS - 1;
  float power = 0.0f;
  for (int i = first; i <= last; i++) {
    float overlap = min(hi, (i + 0.5f) * binWidth) - max(lo, (i - 0.5f) * binWidth);
    if (overlap <= 0.0f) continue;
    power += RTA_fft.readPower(i) * (overlap / binWidth);
  }
  return power;
}

// While enabled, send "RTA <242 hex chars>\n" frames at ~10Hz: one byte per
// band, value = (dB + 100) * 2, i.e. -100dB..+27.5dB in 0.5dB steps. A frame
// is 247 bytes - the ESP's RX line buffer (RX_LINE_MAX in teensy_comm.cpp)
// and the Serial1 TX buffer (espTxBuffer in setup) are both sized for it.
void rtaLoop() {
  if (!rtaStream.enabled()) return;
  if (rtaStream.keepaliveExpired(millis())) {
    setRtaEnabled(false); // logs and re-routes the FFT tap
    return;
  }
  if (!rtaStream.frameDue(millis())) return;
  if (!RTA_fft.available()) return;
  RTA_fft.analyze();

  char frame[4 + RTA_NUM_BANDS * 2 + 1];
  memcpy(frame, "RTA ", 4);
  size_t pos = 4;
  // Band edges are a twelfth of an octave apart: center * 10^(+/-1/80)
  for (int b = 0; b < RTA_NUM_BANDS; b++) {
    float power = rtaBandPower(RTA_BAND_CENTERS[b] * 0.971628f,
                               RTA_BAND_CENTERS[b] * 1.029200f);
    float dB = (power > 1e-10f) ? 10.0f * log10f(power) : -100.0f;
    int v = (int)roundf((dB + 100.0f) * 2.0f);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    frame[pos++] = HEX_DIGITS[v >> 4];
    frame[pos++] = HEX_DIGITS[v & 0x0F];
  }
  frame[pos++] = '\n';

  // Never block on the UART; skip the frame if the TX buffer is busy
  if ((size_t)Serial1.availableForWrite() < pos) return;
  Serial1.write((const uint8_t*)frame, pos);
  rtaStream.markFrameSent(millis());
}

// --- GRM (compressor gain-reduction meter) streaming ---
#define GRM_FRAME_INTERVAL_MS 100
#define GRM_KEEPALIVE_TIMEOUT_MS 7000
static KeepaliveStream grmStream(GRM_FRAME_INTERVAL_MS, GRM_KEEPALIVE_TIMEOUT_MS);

void setGrmEnabled(bool enabled) {
  grmStream.setEnabled(enabled, millis());
}

// While enabled, send "GRM <6 hex chars>\n" frames at ~10Hz: one byte per
// band, value = dB of reduction * 8 (0..31.9dB in 0.125dB steps).
void grmLoop() {
  if (!grmStream.enabled()) return;
  if (grmStream.keepaliveExpired(millis())) {
    grmStream.disable();
    return;
  }
  if (!grmStream.frameDue(millis())) return;

  char frame[4 + COMP_NUM_BANDS * 2 + 2];
  memcpy(frame, "GRM ", 4);
  size_t pos = 4;
  for (int b = 0; b < COMP_NUM_BANDS; b++) {
    int v = (int)roundf(inputComp.gainReductionDb(b) * 8.0f);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    frame[pos++] = HEX_DIGITS[v >> 4];
    frame[pos++] = HEX_DIGITS[v & 0x0F];
  }
  frame[pos++] = '\n';

  if ((size_t)Serial1.availableForWrite() < pos) return;
  Serial1.write((const uint8_t*)frame, pos);
  grmStream.markFrameSent(millis());
}

// --- VU (input bus peak meter) streaming ---
// Frames go out at 20Hz as "VU llrrf\n": one byte per channel mapping peak
// dBFS -60..0 onto 0..255 (0 = silence), plus one hex flag digit (bit0 =
// left clipped, bit1 = right clipped - a flat-topped run of full-scale
// samples, not just a peak touching 0dBFS; see PeakMeter.h). Peaks
// accumulate max-wise in the meter between frames, so nothing is missed.
#define VU_FRAME_INTERVAL_MS 50
#define VU_KEEPALIVE_TIMEOUT_MS 7000
static KeepaliveStream vuStream(VU_FRAME_INTERVAL_MS, VU_KEEPALIVE_TIMEOUT_MS);

void setVuEnabled(bool enabled) {
  vuStream.setEnabled(enabled, millis());
}

static uint8_t vuByte(float peak) {
  if (peak <= 0.001f) return 0; // below -60dBFS
  float dB = 20.0f * log10f(peak);
  int v = (int)roundf((dB + 60.0f) * (255.0f / 60.0f));
  return (uint8_t)constrain(v, 0, 255);
}

void vuLoop() {
  if (!vuStream.enabled()) return;
  if (vuStream.keepaliveExpired(millis())) {
    vuStream.disable();
    return;
  }
  if (!vuStream.frameDue(millis())) return;

  PeakMeter::Reading r = inputMeter.read();
  char frame[16];
  int len = snprintf(frame, sizeof(frame), "VU %02x%02x%x\n",
                     vuByte(r.peak[0]), vuByte(r.peak[1]),
                     (r.clip[0] ? 1 : 0) | (r.clip[1] ? 2 : 0));

  // Never block on the UART; skip the frame if the TX buffer is busy
  if (Serial1.availableForWrite() < len) return;
  Serial1.write((const uint8_t*)frame, len);
  vuStream.markFrameSent(millis());
}
