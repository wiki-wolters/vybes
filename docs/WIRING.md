# ESP8266 ↔ Teensy UART wiring

The ESP8266 and Teensy now talk over a **UART serial link** (115200 baud, 3.3V
logic) instead of I2C. I2C on the ESP is still used, but only for the 1602
LCD backpack.

## Wiring changes

| From (ESP8266 NodeMCU)  | To (Teensy 4.0)  | Purpose                        |
|-------------------------|------------------|--------------------------------|
| **D8** (GPIO15, TX)     | **Pin 0** (RX1)  | ESP → Teensy commands          |
| **D7** (GPIO13, RX)     | **Pin 1** (TX1)  | Teensy → ESP replies/events    |
| GND                     | GND              | Common ground (probably already present) |

Both boards are 3.3V logic, so the lines connect directly — no level shifter.

Also:

1. **Remove** the old I2C wires between the ESP (D1/D2) and the Teensy
   (pins 18/19). The LCD stays on the ESP's D1 (SCL) / D2 (SDA).
2. **Move the front button** from **D7 to D5** (GPIO14). D7 is now the UART
   RX line. Same wiring as before: button between D5 and GND (the pin uses an
   internal pull-up).

## Debug console moved

UART0 (the pins connected to the USB serial converter) is now the Teensy
link, so the ESP's debug output no longer appears on the USB port. Debug logs
are on **D4 (GPIO2), TX only, 115200 baud** — connect a USB-serial adapter's
RX to D4 if you want to watch them. Flashing over USB works exactly as before
(the bootloader uses the original pins).

The Teensy's debug output is unchanged (its own USB serial port).

## Boot notes

- GPIO15 (D8) must be low at boot for the ESP to start normally. The NodeMCU
  has an onboard pull-down and the Teensy RX pin is high-impedance, so this
  works — but don't add a pull-up to this line.
- On boot the Teensy sends `EVENT boot`, and the ESP responds by pushing the
  full DSP state (preset parameters, EQ, gains, FIR files). The ESP also
  pings every 5s as a fallback, so either device can restart independently
  and the system converges.

## Protocol (for reference)

Newline-delimited text, same command vocabulary as before:

- ESP → Teensy: `setEq 3 1000.0 1.00 -3.0`, `setVolume 0.45`, `ping`, ...
- Teensy → ESP: `PONG <uptime-ms>`, `EVENT boot`, and for `getFiles`:
  a `FILES` line, one filename per line, then `EOT`.

## FIR latency compensation

The Teensy now automatically pads the delay lines so channels with different
FIR tap counts stay time-aligned (a linear-phase FIR delays its channel by
(taps−1)/2 samples ≈ 23ms at 2048 taps). If your existing speaker-delay
settings were manually tuned to absorb FIR latency, re-check them after this
update. If you use minimum-phase FIR files (which have no inherent latency),
this compensation will overshoot — say so and it can be made configurable.
