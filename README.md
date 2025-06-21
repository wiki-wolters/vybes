# Vybes, a home DSP project

Vybes is a DSP, designed for flexibility and multiple presets for room correction, EQ curve preferences, dynamic EQ adjustments based on volume, phase syncing, and subwoofer crossover, all controlled via a web interface over WIFI.

## Internals
At the core of the device is a Teensy 4.0 with a 600MHz processor. The Teensy is responsible for all the digital audio manipulations, including delays and parametric EQ.

An ESP8266 NodeMCU development board is responsible for connecting to WIFI, serving a web UI and API, and managing Teensy state. The ESP8266 flash memory will be used to remember preset configurations and for serving up assets for the web UI.

An SPDIF Toslink optical input with digital I2S output allows for direct digital audio into the Teensy without losing audio quality converting from analogue.

Output consists of four RCAs connected to two PCM5102A DAC boards. Two will be used for L & R channels, and one will be used as a subwoofer output. The fourth will be connected, but not used at this point. It could be used for a second subwoofer st some point in the future.

To ensure a smooth power supply, a linear regulator will take a 5V input from a standard power supply via USB-C and output a stable 3.3V that can be used by the DACs.

An internal microphone (ICS-43434 with digital I2S output) connected to the Teensy can detect room volume levels so that EQ can be adjusted dynamically.

A 1602A backlit LCD screen will show the current preset name. The backlight turns on when the preset changes, and turns off after 5 seconds of inactivity.

A button on the front will allow changing between presets and if held for more than 5 seconds, will put the device into config portal state so it can be connected to by an iPhone and authentication details provided for the local network. The ESP8266 will then register itself on the network using MDSN so that it can be accessed on http://vybes.local. (see https://github.com/tzapu/WiFiManager)

## Data Structure for presets
* Preset name
* Speaker delays
  * Left
  * Right
  * Sub
* Subwoofer crossover
  * Frequency
  * Slope (12db, 24db)
* Room Correction: Array of PEQ sets, where each set contains:
  * SPL value (can be null)
  * PEQ set: Array of PEQ points, where each point contains:
    *  Frequency
    *  Gain
    *  Q
*  Preference Curve: Same as Room Correction, can be copied from presets like "Harmon"
*  Equal loudness (on/off)

## Web UI Pages
### Calibrate
This page shows if no calibration has been completed. It must be done before any presets are configured. It contains an input for entering an SPL value and a button to toggle pink noise.

### Home
This page allows the user to quickly perform actions using buttons, sliders and switches. Each top level bullet point is contained in its own box to show a relation between inputs.

* Presets: a button for each, and a plus icon to add new.
  * Current preset button shows in an active state. When in an active state, the preset shows icons on the right:
    * edit: when tapped, navigates to the preset configuration page
    * copy: when tapped, creates a new preset with all settings copied across from the active preset. The new preset name is the same, but with " Copy" appended. The new preset becomes the active preset.
* Turn on/off the subwoofer output (switch)
* Bypass DSP (switch)
* Mute
  * Percentage (slider)
  * On/off (switch)
* Calibration value (text)
* Tools (button, opens tools page)

### Tools
This page is for utilities that don't belong to a specific preset but can be useful.

* Tone generator
  * Frequency slider
  * Volume slider
  * Text value of current frequency  
* Pink noise generator toggle button

### Preset configuration
This page is for editing the properties of an individual preset.

* Name: input, saves immediately on blur
* Speaker delays:
  * Inputs for each speaker, in ms
  * Auto: button, helps to calculate speaker distances and group delay by playing a 100hz pulse for 200ms on each output (L, R, sub), with a 300ms silence inbetween. The pulses can be recorded from the iPhone mic at the listening position and measurements can be taken to determine appropriate speaker delay values, which can then be populated into the speaker inputs
* Room correction: shows a list of PEQ sets, where each list item shows the SPL value for the set, and a small chart showing a preview of the EQ curve
  * Tapping on a list item expands it, showing:
    * An input to edit the SPL value. Changing the SPL value results in the existing set being deleted and a new one created for this SPL. An SPL cannot be chosen that already exists in the list.
    * An interactive PEQ chart, with a tappable circle for each point, and an overall calculated curve. The chart shows a plus icon in the top right corner that allows another point to be added.
    * Freq: slider & input, to adjust value for selected point
    * Gain: slider & input, to adjust value for selected point
    * Q: slider & input, to adjust value for selected point
* Preference curve: same interface as room correction
* Equal loudness: switch

## API Endpoints

### System Status
* **GET /status**
  * Returns current system status including:
    * Speaker gains (left, right, sub)
    * Mute state and percentage
    * Tone generation settings
    * Noise volume
    * Current active preset

### Speaker Controls
* **PUT /speaker/{speaker}/gain/{gain}**
  * speaker: "left", "right", or "sub"
  * gain: 0.0 to 2.0
* **PUT /mute/{state}**
  * state: "on" or "off"
* **PUT /mute/percent/{percent}**
  * percent: 1-100
* **PUT /generate/noise/{volume}**
  * volume: 0-100 (0 turns off noise)

### Preset Management
* **GET /presets**
  * Returns array of all presets with names and current status
* **GET /preset/{name}**
  * Returns complete preset configuration including:
    * FIR filter settings
    * Speaker delays
    * Crossover settings
    * EQ configurations (room correction and preference curves)
* **POST /preset/create/{name}**
  * Creates a new preset with default settings
  * name: must be unique
* **POST /preset/copy/{source}/{new}**
  * source: name of preset to copy
  * new: name for new preset
* **PUT /preset/rename/{old}/{new}**
  * old: current preset name
  * new: new preset name
* **DELETE /preset/{name}**
  * Deletes the specified preset

### Speaker Configuration
* **PUT /preset/{name}/delay/{speaker}/{us}**
  * name: preset name
  * speaker: "left", "right", or "sub"
  * us: delay in microseconds (float)
* **PUT /preset/{name}/delay/enabled/{state}**
  * name: preset name
  * state: "on" or "off"

### FIR Filter Management
* **GET /fir/files**
  * Returns list of available FIR filter files
* **PUT /preset/{name}/fir/file/{channel}/{filter}**
  * name: preset name
  * channel: "left", "right", or "sub"
  * filter: name of FIR filter file
* **PUT /preset/{name}/fir/enabled/{state}**
  * name: preset name
  * state: "on" or "off"

### EQ Management
* **POST /preset/{name}/eq**
  * Creates or updates an EQ set
  * Body: { peqPoints: Array }
* **POST /preset/{name}/eq/{type}/{spl}**
  * type: "room" or "pref"
  * spl: 0-120 (0 = default set)
  * body: Array of PEQ points
* **DELETE /preset/{name}/eq/{type}/{spl}**
  * type: "room" or "pref"
  * spl: SPL value of set to delete
* **PUT /preset/{name}/eq/{type}/enabled/{state}**
  * type: "room" or "pref"
  * state: "on" or "off"

### Crossover Configuration
* **PUT /preset/{name}/crossover/freq/{freq}**
  * name: preset name
  * freq: 20-500 Hz
* **PUT /preset/{name}/crossover/enabled/{state}**
  * name: preset name
  * state: "on" or "off"

### System Controls
* **PUT /preset/active/{name}**
  * name: preset name to activate
* **PUT /generate/noise/{volume}**
  * volume: 0-100 (0 turns off noise)

### Live Updates (WebSocket)
* **ws://vybes.local:8080**
  * Messages are JSON objects with event types:
    * speaker_gain: { speaker: string, gain: number }
    * mute: { state: string }
    * mute_percent: { percent: number }
    * preset: { action: string, name: string, [additional fields] }
    * speaker_delay: { speaker: string, delayUs: number, preset: string }
    * fir_enabled: { preset: string }
    * fir_updated: { preset: string, channel: string, filter: string }
    * eq_updated: { preset: string, type: string, spl: number, peqPoints: Array }
    * eq_created: { preset: string, type: string, spl: number, peqPoints: Array }
    * eq_deleted: { preset: string, type: string, spl: number }
    * crossover: { preset: string, frequency: number, slope: string }
    * noise: { volume: number }

## Directories
* /ESP: ESP8266 API server
* /Teensy: Teensy DSP code
* /mock-server: Mock server for API development
* /WebUI: Web UI code
