# Vybes wiring

Full connection reference for everything the firmware touches. The ESP32 and
Teensy talk over a **UART serial link** (115200 baud, 3.3V logic); I2C on the
ESP is used only for the 1602 LCD backpack.

```
              WIFI / web UI                            audio in: SPDIF, I2S (BT), USB, analog
                   │                                   audio out: octal I2S DACs, SPDIF
             ┌─────┴──────┐      UART 115200       ┌─────────────┐
  LCD, IR,   │   ESP32    │  GPIO17 ──────→ pin 0  │  Teensy 4.1 │── SD card
  button ────│  DevKitC   │  GPIO16 ←────── pin 1  │             │   (built-in slot)
             └────────────┘                        └─────────────┘
```

## ESP32 pins

Two boards are supported; pin maps live in `ESP/esp-web-server/board_pins.h`
and the build env picks the right one (`pio run -d ESP` for the classic
DevKitC, `-e esp32s3` for the S3 — see `ESP/platformio.ini`).

| Signal                          | ESP32 DevKitC (WROOM-32) | ESP32-S3 DevKitC-1 | Purpose |
|---------------------------------|--------------------------|--------------------|---------|
| TX2 → Teensy **pin 0** (RX1)    | GPIO **17**              | GPIO **17**        | ESP → Teensy commands (UART2, 115200) |
| RX2 ← Teensy **pin 1** (TX1)    | GPIO **16**              | GPIO **16**        | Teensy → ESP replies/events |
| LCD backpack SCL                | GPIO **22**              | GPIO **9**         | I2C clock — 1602 LCD via PCF8574 @ 0x27 |
| LCD backpack SDA                | GPIO **21**              | GPIO **8**         | I2C data |
| IR receiver data out            | GPIO **4**               | GPIO **4**         | IR remote input (`remote_control.cpp`) |
| Front button (other leg to GND) | GPIO **32**              | GPIO **5**         | Preset cycling / pairing (internal pull-up) |
| Bluetooth module pairing input  | GPIO **33**              | GPIO **6**         | Driven HIGH while the button is held (`button.cpp`) |
| Debug console + flashing        | USB port                 | USB port labeled "UART" | 115200 baud |
| Common ground                   | GND                      | GND                | Shared with Teensy, DACs, BT module |

When rewiring onto other pins, avoid:

- **Classic ESP32**: GPIO 0, 2, 5, 12, 15 (boot strapping) and 34-39
  (input-only, no internal pull-ups)
- **ESP32-S3**: GPIO 0, 3, 45, 46 (strapping), 19/20 (native USB),
  26-32 (SPI flash) and 33-37 (used by octal-PSRAM modules)

## Teensy 4.1 pins

These pins are fixed by the Audio library objects used in
`Teensy/fir_filters/fir_filters.ino` — they are not configurable.

### Outputs

Analog output is octal I2S (`AudioOutputI2SOct`): up to four stereo DAC
boards on four data lines, all sharing the I2S1 clocks. Every DAC board
wires its BCK to pin 21 and LCK to pin 20; only DIN differs per board.

| Teensy pin | Signal        | Connects to                | Purpose |
|------------|---------------|----------------------------|---------|
| **7**      | OUT1A (data)  | DAC board 1 (PCM5102A) DIN | Channels 1-2: main L & R |
| **32**     | OUT1B (data)  | DAC board 2 (PCM5102A) DIN | Channels 3-4: subwoofer, mirrored to both outputs (two subs supported) |
| **6**      | OUT1C (data)  | DAC board 3 DIN (future)   | Channels 5-6: unused |
| **9**      | OUT1D (data)  | DAC board 4 DIN (future)   | Channels 7-8: unused |
| **21**     | BCLK1         | All DAC boards BCK         | I2S1 bit clock (shared with BT input) |
| **20**     | LRCLK1        | All DAC boards LCK         | I2S1 word clock (shared with BT input) |
| **14**     | SPDIF OUT     | Toslink transmitter        | `AudioOutputSPDIF3` pass-through |

The subwoofer signal is mirrored to both outputs of DAC board 2 (same as
the old `AudioOutputI2S2` wiring), so one or two subs can be connected.
Moving all analog outputs onto I2S1 freed the I2S2 port, which now hosts
the analog line-in ADC (see Inputs); its output side (pin 2) is still
unused.

### Inputs

| Teensy pin | Signal       | Connects to               | Purpose |
|------------|--------------|---------------------------|---------|
| **15**     | SPDIF IN     | Toslink receiver          | `AsyncAudioInputSPDIF3` optical input |
| **8**      | IN1 (data)   | Bluetooth receiver I2S out| `AudioInputI2S` — BT audio |
| **5**      | IN2 (data)   | ADC (PCM1808) DOUT        | `AudioInputI2S2` — analog line-in |
| **4**      | BCLK2        | ADC BCK                   | I2S2 bit clock |
| **3**      | LRCLK2       | ADC LRC                   | I2S2 word clock |
| **33**     | MCLK2        | ADC SCK/SCKI              | I2S2 master clock (the PCM1808 needs it) |
| USB port   | —            | Source device             | `AudioInputUSB` (USB sound card) |

The Bluetooth input shares the I2S1 clocks (BCLK1 = 21, LRCLK1 = 20) with the
L/R DAC. The Teensy is the I2S clock master on that bus, so the Bluetooth
board must be wired to those same clock lines and run as an I2S slave.

The analog line-in ADC lives on I2S2, where the Teensy is also clock master.
A PCM1808 breakout works as-is: it defaults to I2S slave format and takes
its system clock from MCLK2 (pin 33, 256×fs). Wire VCC (3.3V), GND, and the
four signals per the table; line-level sources connect straight to the
board's L/R inputs.

### Other

| Connection      | Purpose |
|-----------------|---------|
| Built-in SD slot| FIR filter files (`SD.begin(BUILTIN_SDCARD)`) |
| Pins **0 / 1**  | Serial1 link to the ESP32 (see above) |
| USB port        | Debug serial console + USB audio (built with `USB_MIDI_AUDIO_SERIAL`) |

### PCM5102A DAC boards

All DAC boards are PCM5102A modules: wire VIN (3.3V), GND, BCK, LCK and DIN
per the tables above. BCK and LCK are daisy-chained across every board;
each board gets its own DIN line. The Teensy does not supply a master
clock — leave the module's SCK tied to GND (most boards do this on-board)
so the DAC generates its own. Keep the shared clock runs short; a 100 Ω
series resistor at the Teensy end of BCK and LCK is cheap insurance
against ringing once several boards hang off them.

## Front button & Bluetooth pairing

The button (GPIO32 → GND) is polled with an internal pull-up:

- **Short press**: wakes the LCD backlight; further presses cycle presets.
  The selected preset is applied ~1s after the last press.
- **Hold > 0.5s**: GPIO33 goes HIGH (Bluetooth pairing line) and the screen
  prompts "Hold for 3s to pair"; after ~3.6s it shows "Pairing mode...".
  GPIO33 returns LOW on release.

## Debug console

The ESP32 has enough UARTs that the Teensy link lives on UART2, so debug
output is simply on the **USB port at 115200 baud**
(`pio device monitor -b 115200`). The Teensy's debug output is on its own
USB serial port.

## HTTPS certificates

The web server also listens on HTTPS (port 443) when a certificate exists on
LittleFS at `/certs/server.crt` + `/certs/server.key`. Generate one with
`ESP/make-certs.sh` (mkcert) and upload it with `pio run -d ESP -t uploadfs`;
without certs the device serves plain HTTP only. HTTPS is what lets browsers
grant microphone access to the analyzer page — trust the mkcert root CA on
each phone/laptop that should use the mic (steps in the script header).

## Boot notes

- On boot the Teensy sends `EVENT boot`, and the ESP responds by pushing the
  full DSP state (preset parameters, EQ, gains, FIR files). The ESP also
  pings every 5s as a fallback, so either device can restart independently
  and the system converges.

## Protocol (for reference)

Newline-delimited text, same command vocabulary as before:

- ESP → Teensy: `setEq 3 1000.0 1.00 -3.0`, `setVolume 0.45`, `ping`, ...
- Teensy → ESP: `PONG <uptime-ms>`, `EVENT boot`, and for `getFiles`:
  a `FILES` line, one filename per line, then `EOT`.

## FIR engine and latency compensation

The FIR filters run through a fast convolution engine (uniformly partitioned
overlap-save: the filter is split into 128-tap partitions convolved in the
frequency domain), which supports filters up to 4096 taps per channel using
a few percent of the CPU. `FIR_USE_FAST_CONVOLUTION` in
`Teensy/fir_filters/fir_filters.ino` switches back to the original
direct-form engine (max 2048 taps, CPU-bound); both engines produce
identical, sample-aligned output.

The Teensy automatically pads the delay lines so channels with different
FIR tap counts stay time-aligned (a linear-phase FIR delays its channel by
(taps−1)/2 samples ≈ 23ms at 2048 taps, ≈ 46ms at 4096). If your existing
speaker-delay settings were manually tuned to absorb FIR latency, re-check
them after this update. If you use minimum-phase FIR files (which have no
inherent latency), this compensation will overshoot — say so and it can be
made configurable.
