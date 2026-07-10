# Vybes, a home DSP project

Vybes is a DSP, designed for flexibility and multiple presets covering parametric EQ, FIR
filter room correction, speaker delays, and subwoofer crossover, all controlled via a web
interface over WIFI.

## Internals

At the core of the device is a Teensy 4.1 with a 600MHz processor. The Teensy is
responsible for all the digital audio manipulation: input mixing, parametric EQ, FIR
filtering, speaker delays, subwoofer crossover, and tone/pink-noise generation.

An ESP8266 NodeMCU development board is responsible for connecting to WIFI, serving the
web UI and API, and managing Teensy state. Preset configuration is stored on the
ESP8266's flash (LittleFS, as MessagePack), alongside the built web UI assets.

Audio inputs (all digital, into the Teensy):
* SPDIF Toslink optical
* Bluetooth receiver via I2S
* USB audio (the Teensy enumerates as a USB sound card; built with `USB_MIDI_AUDIO_SERIAL`)

Audio outputs:
* Analog L & R via an I2S DAC board (PCM5102A)
* Analog subwoofer via a second PCM5102A I2S DAC board
* SPDIF pass-through out

Other hardware handled by the ESP8266:
* A 1602 backlit LCD (via an I2C PCF8574 backpack) shows the current preset name. The
  backlight turns on when the preset changes and turns off after a few seconds.
* A front button: a short press wakes the LCD backlight, further presses cycle through
  presets (the selection is applied one second after the last press). Holding the button
  for ~3 seconds triggers Bluetooth pairing (the ESP drives the Bluetooth module's
  pairing line high while held).
* An IR receiver, so a spare remote control can drive volume up/down, mute, and
  next/previous preset.

WIFI credentials are provisioned by WiFiManager: if the ESP8266 can't connect, it
automatically opens a "Vybes-Config" access point that a phone can join to enter
credentials. Once on the network it registers itself via mDNS so the device is
reachable at http://vybes.local.

The config portal also has a "Standalone mode (no router)" button for use away from
any WIFI network (e.g. in the car): it brings up a plain "Vybes" access point
(password `vybes-dsp`) serving the full web UI at http://192.168.4.1. The AP
deliberately has no captive portal or internet, so phones keep streaming (Spotify,
Bluetooth audio) over cellular while connected to it — confirm the "no internet,
stay connected?" prompt once. Standalone mode lasts until the next power cycle.

FIR filter files live on an SD card in the Teensy 4.1's built-in slot; the ESP asks the
Teensy for the list of available files and tells it which file to load per channel.

## Presets

Up to 8 presets are stored (see `ESP/esp-web-server/config.h`). Each preset contains:

* Name (max 15 characters)
* Speaker gains (left, right, sub)
* Speaker delays in microseconds (left, right, sub) + enabled flag
* Subwoofer crossover: frequency + enabled flag
* Preference curve EQ: up to 3 PEQ sets of up to 15 points each (frequency, gain, Q),
  plus an enabled flag. Each set carries an SPL value for future volume-dependent EQ,
  but currently only the default set (spl = 0) is used.
* FIR filters: a filter filename per channel (left, right, sub) + enabled flag

Global (non-preset) state includes master volume, mute state and mute percentage,
input gains (spdif, bluetooth, usb, tone), and the tone/noise generator settings.

## Web UI

A Vue 3 + Vite single-page app (in `/WebUI`), served by the ESP8266. Three views:

### Home
* Presets: a button for each, and a plus icon to add new. Tapping the active preset's
  edit icon navigates to the preset editor.
* Master volume slider
* Input source gain sliders: Bluetooth, TV (SPDIF), USB, Tone
* Speaker on/off toggles: left, right, subwoofer
* Mute: volume-reduction percentage slider and on/off toggle
* Configuration: backup and restore buttons (download/upload the full device config)

### Tools
* Tone generator: frequency and volume sliders with a start/stop button
* Pink noise generator: volume slider with a start/stop button

### Preset editor
Rename, copy, and delete buttons for the selected preset, plus collapsible sections
(each with its own enable toggle):
* EQ: interactive parametric EQ chart with draggable points and a calculated
  frequency-response curve
* FIR filters: a file selector per channel (left, right, sub)
* Subwoofer crossover: frequency slider
* Speaker delays: an input per speaker, in microseconds

## API

HTTP on port 80. Parameters are query strings unless a JSON body is noted.

### System
* **GET /status** — current state: speaker gains, input gains, mute, tone and noise
  generator settings, master volume, and the active preset name
* **PUT /volume?value={0-100}** — master volume
* **PUT /mute?state={on|off}**
* **PUT /mute/percent?percent={0-100}** — how much mute reduces the volume
* **GET /backup** — download the device configuration (MessagePack)
* **POST /restore** — upload a previously downloaded configuration (multipart file)

### Gains
* **PUT /gains/speaker?speaker={left|right|sub}&value={gain}**
* **PUT /gains/input** — JSON body, any of: `{ "spdif": n, "bluetooth": n, "usb": n, "tone": n }`
* **GET /preset/gains?preset_name={name}**
* **PUT /preset/gains?preset_name={name}** — JSON body of per-speaker gains

### Signal generator
* **PUT /generate/tone?frequency={20-20000}&volume={0-100}**
* **PUT /generate/tone/stop**
* **PUT /noise?level={0-100}** — 0 turns noise off

### Preset management
* **GET /presets** — all presets with names and which is current
* **GET /preset?name={name}** — full preset configuration
* **POST /preset?action=create&name={name}**
* **POST /preset?action=copy&source={name}&destination={name}**
* **PUT /preset?action=rename&old_name={name}&new_name={name}**
* **DELETE /preset?name={name}**
* **PUT /preset/active?name={name}** — switch the active preset

### Preset configuration
* **PUT /preset/delay?preset_name={name}&speaker={left|right|sub}&value={0-10000}** — delay in µs
* **PUT /preset/delay/enabled?preset_name={name}&state={on|off}**
* **PUT /preset/eq?preset_name={name}** — JSON body: array of `{ "freq": 20-20000, "gain": -15-15, "q": 0.1-10 }`
* **PUT /preset/eq/point?preset_name={name}** — JSON body: single point `{ "id": 0-14, "freq": n, "gain": n, "q": n }`
* **PUT /preset/eq/enabled?preset_name={name}&enabled={on|off}**
* **PUT /preset/crossover?preset_name={name}&frequency={20-20000}**
* **PUT /preset/crossover/enabled?preset_name={name}&enabled={on|off}**

### FIR filters
* **GET /fir/files** — list of filter files on the Teensy's SD card
* **PUT /preset/fir?preset_name={name}&speaker={left|right|sub}&file={filename}**
* **PUT /preset/fir/enabled?preset_name={name}&state={on|off}**

### Live updates (WebSocket)
* **ws://vybes.local/live-updates**
  * State changes are broadcast to all connected clients as JSON with a `messageType`
    field: `volumeChanged`, `muteChanged`, `mutePercentChanged`, `activePresetChanged`,
    `delayChanged`, `delayEnabledChanged`, `eqPointsChanged`, `eqEnabledChanged`,
    `crossoverChanged`, `crossoverEnabledChanged`, `firChanged`, `firEnabledChanged`,
    plus payload fields (usually `presetName` and the new value).
  * Tone and noise updates are broadcast as `{ "toneFrequency": n, "toneVolume": n }`
    and `{ "noiseVolume": n }` (no `messageType` field).

## Directories
* `/ESP`: ESP8266 web server firmware (API, WebSocket, LCD, button, IR remote, WIFI)
* `/Teensy`: Teensy DSP firmware. `fir_filters/` is the firmware that builds; `direct/`
  and `fir_test/` are earlier experiments/test sketches.
* `/WebUI`: Vue 3 web UI
* `/mock-server`: Express + SQLite mock of the device API, for developing the web UI
  without hardware (includes the WebSocket endpoint)
* `/docs`: wiring and protocol documentation
* `/3d`: 3D-printable case models

## Building and flashing

Both firmwares build with [PlatformIO](https://platformio.org) — no Arduino IDE needed.
PlatformIO downloads the right toolchains, frameworks, and libraries automatically on
the first build.

### One-time setup

Install the PlatformIO CLI, e.g. one of:

```sh
brew install platformio    # macOS (Homebrew)
pipx install platformio    # anywhere with Python
pip install platformio     # what CI uses
```

(Alternatively, install the "PlatformIO IDE" extension in VS Code, which bundles the
`pio` CLI and adds build/upload buttons.)

The web UI needs Node.js. Install its dependencies once:

```sh
cd WebUI && npm install
```

### Teensy (DSP firmware)

```sh
pio run -d Teensy             # compile only
pio run -d Teensy -t upload   # compile and flash over USB
```

Upload uses the Teensy loader bundled with the PlatformIO Teensy platform. If the
upload sits waiting for the board, press the program button on the Teensy.

### ESP8266 (web server firmware + web UI)

The ESP8266's flash holds two separate images, uploaded by two separate commands:
the firmware (the compiled C++ program) and a LittleFS filesystem image (the built
web UI assets, plus presets saved at runtime).

1. Flash the firmware:

   ```sh
   pio run -d ESP -t upload
   ```

2. Build the web UI and copy it into the ESP's filesystem staging directory
   (`ESP/esp-web-server/data/dist`):

   ```sh
   cd WebUI && npm run deploy && cd ..
   ```

3. Pack `ESP/esp-web-server/data` into a LittleFS image and flash it:

   ```sh
   pio run -d ESP -t uploadfs
   ```

Note: `uploadfs` replaces the *entire* filesystem, including presets saved on the
device. To keep them, download **GET /backup** before flashing and **POST /restore**
afterwards.

Both upload targets auto-detect the USB serial port. If more than one device is
plugged in, find the right port with `pio device list` and pass
`--upload-port <port>`.

To watch ESP debug output: `pio device monitor -b 115200` (the ESP's debug UART is on
D4/GPIO2 — see [docs/WIRING.md](docs/WIRING.md) for the pinout).

GitHub Actions compiles all three on every push (`.github/workflows/build.yml`).

## ESP ↔ Teensy link

The two boards communicate over UART (115200 baud) using a newline-delimited text
protocol. On boot the Teensy sends `EVENT boot` and the ESP pushes the full DSP state,
so either device can restart independently and the system converges. See
[docs/WIRING.md](docs/WIRING.md) for the wiring, the serial protocol, and the
debug-console pinout.
