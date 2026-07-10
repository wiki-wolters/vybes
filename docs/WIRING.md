# Vybes wiring

Full connection reference for everything the firmware touches. The ESP8266 and
Teensy talk over a **UART serial link** (115200 baud, 3.3V logic); I2C on the
ESP is used only for the 1602 LCD backpack.

```
              WIFI / web UI                        audio in: SPDIF, I2S (BT), USB
                   │                               audio out: 2x I2S DAC, SPDIF
             ┌─────┴──────┐    UART 115200     ┌─────────────┐
  LCD, IR,   │  ESP8266   │  D8 ──────→ pin 0  │  Teensy 4.1 │── SD card
  button ────│  NodeMCU   │  D7 ←────── pin 1  │             │   (built-in slot)
             └────────────┘                    └─────────────┘
```

## ESP8266 NodeMCU pins

| NodeMCU pin | GPIO   | Connects to                    | Purpose |
|-------------|--------|--------------------------------|---------|
| **D8** (TX) | GPIO15 | Teensy **pin 0** (RX1)         | ESP → Teensy commands (UART, 115200) |
| **D7** (RX) | GPIO13 | Teensy **pin 1** (TX1)         | Teensy → ESP replies/events |
| **D1**      | GPIO5  | LCD backpack SCL               | I2C clock — 1602 LCD via PCF8574 @ 0x27 |
| **D2**      | GPIO4  | LCD backpack SDA               | I2C data |
| **D3**      | GPIO0  | IR receiver data out           | IR remote input (`remote_control.cpp`) |
| **D4**      | GPIO2  | USB-serial adapter RX (opt.)   | Debug console, TX-only, 115200 |
| **D5**      | GPIO14 | Front button, other leg to GND | Preset cycling / pairing (internal pull-up) |
| **D6**      | GPIO12 | Bluetooth module pairing input | Driven HIGH while the button is held (`button.cpp`) |
| GND         | —      | Common ground                  | Shared with Teensy, DACs, BT module |

## Teensy 4.1 pins

These pins are fixed by the Audio library objects used in
`Teensy/fir_filters/fir_filters.ino` — they are not configurable.

### Outputs

| Teensy pin | Signal        | Connects to                | Purpose |
|------------|---------------|----------------------------|---------|
| **7**      | OUT1A (data)  | L/R DAC (PCM5102A) DIN     | `AudioOutputI2S` — main L & R |
| **21**     | BCLK1         | L/R DAC BCK                | I2S1 bit clock (shared with BT input) |
| **20**     | LRCLK1        | L/R DAC LCK                | I2S1 word clock (shared with BT input) |
| **2**      | OUT2 (data)   | Sub DAC (PCM5102A) DIN     | `AudioOutputI2S2` — subwoofer |
| **4**      | BCLK2         | Sub DAC BCK                | I2S2 bit clock |
| **3**      | LRCLK2        | Sub DAC LCK                | I2S2 word clock |
| **14**     | SPDIF OUT     | Toslink transmitter        | `AudioOutputSPDIF3` pass-through |

### Inputs

| Teensy pin | Signal       | Connects to               | Purpose |
|------------|--------------|---------------------------|---------|
| **15**     | SPDIF IN     | Toslink receiver          | `AsyncAudioInputSPDIF3` optical input |
| **8**      | IN1 (data)   | Bluetooth receiver I2S out| `AudioInputI2S` — BT audio |
| USB port   | —            | Source device             | `AudioInputUSB` (USB sound card) |

The Bluetooth input shares the I2S1 clocks (BCLK1 = 21, LRCLK1 = 20) with the
L/R DAC. The Teensy is the I2S clock master on that bus, so the Bluetooth
board must be wired to those same clock lines and run as an I2S slave.

### Other

| Connection      | Purpose |
|-----------------|---------|
| Built-in SD slot| FIR filter files (`SD.begin(BUILTIN_SDCARD)`) |
| Pins **0 / 1**  | Serial1 link to the ESP8266 (see above) |
| USB port        | Debug serial console + USB audio (built with `USB_MIDI_AUDIO_SERIAL`) |

### PCM5102A DAC boards

Both DAC boards are PCM5102A modules: wire VIN (3.3V), GND, BCK, LCK and DIN
per the tables above. The Teensy does not supply a master clock — leave the
module's SCK tied to GND (most boards do this on-board) so the DAC generates
its own.

## Front button & Bluetooth pairing

The button (D5 → GND) is polled with an internal pull-up:

- **Short press**: wakes the LCD backlight; further presses cycle presets.
  The selected preset is applied ~1s after the last press.
- **Hold > 0.5s**: D6 goes HIGH (Bluetooth pairing line) and the screen
  prompts "Hold for 3s to pair"; after ~3.6s it shows "Pairing mode...".
  D6 returns LOW on release.

## Debug console

UART0 (the pins connected to the USB serial converter) is the Teensy link, so
the ESP's debug output does not appear on the USB port. Debug logs are on
**D4 (GPIO2), TX only, 115200 baud** — connect a USB-serial adapter's RX to D4
to watch them. Flashing over USB works exactly as before (the bootloader uses
the original pins).

The Teensy's debug output is on its own USB serial port.

## Boot notes

- GPIO15 (D8) must be low at boot for the ESP to start normally. The NodeMCU
  has an onboard pull-down and the Teensy RX pin is high-impedance, so this
  works — but don't add a pull-up to this line.
- On boot the Teensy sends `EVENT boot`, and the ESP responds by pushing the
  full DSP state (preset parameters, EQ, gains, FIR files). The ESP also
  pings every 5s as a fallback, so either device can restart independently
  and the system converges.

## Migrating from the old I2C link

If a board is still wired for the original I2C design:

1. **Remove** the old I2C wires between the ESP (D1/D2) and the Teensy
   (pins 18/19). The LCD stays on the ESP's D1 (SCL) / D2 (SDA).
2. **Move the front button** from **D7 to D5** (GPIO14). D7 is now the UART
   RX line. Same wiring as before: button between D5 and GND.
3. Add the two UART wires per the table above (D8 → pin 0, D7 ← pin 1).

## Protocol (for reference)

Newline-delimited text, same command vocabulary as before:

- ESP → Teensy: `setEq 3 1000.0 1.00 -3.0`, `setVolume 0.45`, `ping`, ...
- Teensy → ESP: `PONG <uptime-ms>`, `EVENT boot`, and for `getFiles`:
  a `FILES` line, one filename per line, then `EOT`.

## FIR latency compensation

The Teensy automatically pads the delay lines so channels with different
FIR tap counts stay time-aligned (a linear-phase FIR delays its channel by
(taps−1)/2 samples ≈ 23ms at 2048 taps). If your existing speaker-delay
settings were manually tuned to absorb FIR latency, re-check them after this
update. If you use minimum-phase FIR files (which have no inherent latency),
this compensation will overshoot — say so and it can be made configurable.
