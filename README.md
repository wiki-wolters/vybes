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

## System Architecture
The system consists of three main components:
- **ESP8266 Microcontroller**: Acts as the central hub. It hosts the web interface, communicates with the Teensy to send control parameters and receive data, and serves the Web UI to the user's browser.
- **Teensy Microcontroller**: Responsible for real-time audio processing. It receives EQ parameters from the ESP8266, applies them to the audio signal, and can send processed data or status back.
- **Web User Interface (WebUI)**: A Vue 3 single-page application running in the user's browser. It provides controls for the parametric EQ, sending user adjustments to the ESP8266, which then relays them to the Teensy. The UI is designed to be lightweight to be served from the ESP8266.

## Hardware Wiring Diagram
This section describes the physical wiring between the ESP8266 and the Teensy microcontroller.

**Power Connections:**
- **ESP8266 VIN** to **Teensy VIN/VUSB**: Provides power to the Teensy from the ESP8266's regulated output if available, or share a common 5V source. Ensure voltage compatibility.
- **ESP8266 GND** to **Teensy GND**: Essential common ground connection.

**Serial Communication (Logic Level 3.3V):**
- **ESP8266 TX (GPIO1 or D1 on Wemos D1 Mini)** to **Teensy RX (e.g., Pin 0 on Teensy 4.x - Serial1 RX1)**: Transmits data from ESP8266 to Teensy.
- **ESP8266 RX (GPIO3 or D2 on Wemos D1 Mini)** to **Teensy TX (e.g., Pin 1 on Teensy 4.x - Serial1 TX1)**: Transmits data from Teensy to ESP8266.

*Important Notes:*
- *Logic Levels*: Both ESP8266 and most Teensy boards operate at 3.3V logic levels. Direct connection for serial communication is usually fine. Verify for your specific Teensy model.
- *Serial Port*: The pins mentioned for ESP8266 (GPIO1, GPIO3) are typically used for `Serial`. For Teensy, `Serial1` (Pins 0 and 1) is a common choice for communication with other devices. Adjust pins if using different serial ports.
- *Alternative Power*: If the ESP8266 cannot provide sufficient power, power the Teensy separately via its USB port or VIN pin, but ensure a common ground is always maintained.

```mermaid
graph TD
    %% Main Controllers
    subgraph "ESP8266 (e.g., Wemos D1 Mini / NodeMCU)"
        direction LR
        ESP_VIN["VIN (5V In)"]
        ESP_GND["GND"]
        ESP_3V3["3.3V Out"]
        ESP_TX["TX (GPIO1)"]
        ESP_RX["RX (GPIO3)"]
        ESP_D1_SCL["D1 / GPIO5 (SCL)"]
        ESP_D2_SDA["D2 / GPIO4 (SDA)"]
        ESP_D6_BTN["D6 / GPIO12 (Button In)"]
    end

    subgraph "Teensy (e.g., Teensy 4.0/4.1)"
        direction LR
        TEENSY_VIN["VIN / VUSB (5V In)"]
        TEENSY_GND["GND"]
        TEENSY_3V3["3.3V Out"]
        TEENSY_TX1["TX1 (Pin 1)"]
        TEENSY_RX1["RX1 (Pin 0)"]
        TEENSY_P18_SDA["Pin 18 (SDA - Wire)"]
        TEENSY_P19_SCL["Pin 19 (SCL - Wire)"]
        TEENSY_P8_I2S_DIN["Pin 8 (I2S DIN Mic)"]
        TEENSY_P20_I2S_LRCLK["Pin 20 (I2S LRCLK Mic/SPDIF)"]
        TEENSY_P21_I2S_BCLK["Pin 21 (I2S BCLK Mic/SPDIF)"]
        TEENSY_P11_I2S_MCLK["Pin 11 (I2S MCLK Mic)"]
        TEENSY_P7_SPDIF_DIN["Pin 7 (SPDIF I2S DIN)"]
        %% Placeholder for SPDIF BCLK/LRCLK if different from Mic. For now, assume sharing or careful selection.
        %% TEENSY_P9_SPDIF_BCLK["Pin 9 (SPDIF I2S BCLK)"]
        %% TEENSY_P23_SPDIF_LRCLK["Pin 23 (SPDIF I2S LRCLK)"]
    end

    %% ESP8266 Peripherals
    subgraph "ESP8266 Peripherals"
        direction TB
        LCD["LCD Screen (I2C)"]
        BTN["Front Button"]
        BTN_R_PULLDOWN["10kΩ Pull-down Resistor"]
    end

    %% Teensy Peripherals
    subgraph "Teensy Peripherals"
        direction TB
        DAC1["Stereo DAC 1 (I2C - Addr 1)"]
        DAC2["Stereo DAC 2 (I2C - Addr 2)"]
        I2C_PULLUP_SDA["4.7kΩ Pull-up (SDA)"]
        I2C_PULLUP_SCL["4.7kΩ Pull-up (SCL)"]
        MIC["I2S Microphone"]
        SPDIF_CONVERTER["SPDIF to I2S Converter"]
        OPTICAL_IN["Optical SPDIF Input Jack"]
    end

    %% Power System
    subgraph "Power Source (e.g., USB or 5V Regulator)"
        P5V["+5V"]
        PGND["GND"]
    end

    %% Connections

    %% Power to MCUs
    P5V --> ESP_VIN
    PGND --> ESP_GND
    P5V --> TEENSY_VIN
    PGND --> TEENSY_GND

    %% ESP-Teensy Serial Communication
    ESP_TX --> TEENSY_RX1
    TEENSY_TX1 --> ESP_RX
    ESP_GND --- TEENSY_GND

    %% ESP8266 Peripheral Connections
    ESP_D1_SCL --> LCD
    ESP_D2_SDA --> LCD
    ESP_3V3 --> LCD_VCC[VCC]:::hidden_link
    ESP_GND --> LCD_GND[GND]:::hidden_link
    LCD --- LCD_VCC
    LCD --- LCD_GND

    ESP_3V3 --> BTN_VCC_TERM[Button Terminal 1]:::hidden_link
    BTN_VCC_TERM --- BTN
    BTN --> ESP_D6_BTN
    ESP_D6_BTN --- BTN_R_PULLDOWN
    BTN_R_PULLDOWN --> ESP_GND


    %% Teensy Peripheral Connections
    %% I2C DACs
    TEENSY_P18_SDA --> DAC1
    TEENSY_P19_SCL --> DAC1
    TEENSY_3V3 --> DAC1_VCC[VCC]:::hidden_link
    TEENSY_GND --> DAC1_GND[GND]:::hidden_link
    DAC1 --- DAC1_VCC
    DAC1 --- DAC1_GND

    TEENSY_P18_SDA --> DAC2
    TEENSY_P19_SCL --> DAC2
    TEENSY_3V3 --> DAC2_VCC[VCC]:::hidden_link
    TEENSY_GND --> DAC2_GND[GND]:::hidden_link
    DAC2 --- DAC2_VCC
    DAC2 --- DAC2_GND

    TEENSY_P18_SDA --- I2C_PULLUP_SDA
    I2C_PULLUP_SDA --> TEENSY_3V3
    TEENSY_P19_SCL --- I2C_PULLUP_SCL
    I2C_PULLUP_SCL --> TEENSY_3V3

    %% I2S Microphone
    TEENSY_P8_I2S_DIN <-- MIC_DOUT[DOUT]:::hidden_link
    MIC --- MIC_DOUT
    TEENSY_P20_I2S_LRCLK --> MIC_LRCLK[LRCLK]:::hidden_link
    MIC --- MIC_LRCLK
    TEENSY_P21_I2S_BCLK --> MIC_BCLK[BCLK]:::hidden_link
    MIC --- MIC_BCLK
    TEENSY_P11_I2S_MCLK --> MIC_MCLK[MCLK]:::hidden_link %% Optional MCLK
    MIC --- MIC_MCLK
    TEENSY_3V3 --> MIC_VCC[VCC]:::hidden_link
    TEENSY_GND --> MIC_GND[GND]:::hidden_link
    MIC --- MIC_VCC
    MIC --- MIC_GND

    %% SPDIF Input
    OPTICAL_IN --> SPDIF_CONVERTER
    SPDIF_CONVERTER_VCC[VCC]:::hidden_link --- SPDIF_CONVERTER
    SPDIF_CONVERTER_GND[GND]:::hidden_link --- SPDIF_CONVERTER
    TEENSY_3V3 --> SPDIF_CONVERTER_VCC %% Or 5V, check module
    TEENSY_GND --> SPDIF_CONVERTER_GND

    SPDIF_CONVERTER_DOUT[I2S OUT]:::hidden_link --- SPDIF_CONVERTER
    SPDIF_CONVERTER_DOUT --> TEENSY_P7_SPDIF_DIN
    SPDIF_CONVERTER_BCLK[I2S BCLK]:::hidden_link --- SPDIF_CONVERTER
    SPDIF_CONVERTER_BCLK --> TEENSY_P21_I2S_BCLK %% Assuming shared BCLK with Mic or separate I2S port handling
    SPDIF_CONVERTER_LRCLK[I2S LRCLK]:::hidden_link --- SPDIF_CONVERTER
    SPDIF_CONVERTER_LRCLK --> TEENSY_P20_I2S_LRCLK %% Assuming shared LRCLK with Mic or separate I2S port handling

    %% Styling
    classDef mcu fill:#f9f,stroke:#333,stroke-width:2px;
    class ESP8266,Teensy mcu;
    classDef peripheral fill:#lightblue,stroke:#333,stroke-width:2px;
    class LCD,BTN,DAC1,DAC2,MIC,SPDIF_CONVERTER,OPTICAL_IN peripheral;
    classDef passive fill:#lightgrey,stroke:#333;
    class BTN_R_PULLDOWN,I2C_PULLUP_SDA,I2C_PULLUP_SCL passive;
    classDef power fill:#orange,stroke:#333;
    class P5V,PGND power;

    linkStyle default stroke-width:1px;
    %% Hidden links for layout
    linkStyle hidden_link stroke-width:0px, interpolate:linear;
```

### Teensy Peripheral Connections (Example: Teensy 4.0/4.1)

Power and ground connections for these peripherals should be made to the Teensy's 3.3V output and GND pins, unless the peripheral specifically requires 5V and has its own regulation.

**1. Stereo DAC Boards (x2 via I2C):**
   - Purpose: Convert digital audio signals from the Teensy to analog for output.
   - Interface: I2C
   - Teensy Pins:
       - SDA: Pin 18 (Default I2C - `Wire`)
       - SCL: Pin 19 (Default I2C - `Wire`)
   - Wiring:
       - Connect SDA (Pin 18) to the SDA pins of both DAC boards.
       - Connect SCL (Pin 19) to the SCL pins of both DAC boards.
       - Connect the VCC pin of each DAC board to Teensy's 3.3V.
       - Connect the GND pin of each DAC board to Teensy's GND.
   - Addressing: Each DAC board must be configured to have a unique I2C address. This is typically done via address selection pins/jumpers on the DAC modules themselves. Consult your DAC module's datasheet.
   - Pull-up Resistors: Add external 4.7kΩ pull-up resistors from SDA to 3.3V and from SCL to 3.3V. These are shared by all devices on the I2C bus.

**2. Microphone (I2S):**
   - Purpose: Capture audio input.
   - Interface: I2S
   - Teensy Pins (using primary I2S, assuming `AudioInputI2S` object):
       - BCLK (Bit Clock): Pin 21
       - LRCLK (Left/Right Clock or Word Select): Pin 20
       - DIN (Data In from Microphone): Pin 8
       - MCLK (Master Clock): Pin 11 (Note: Some I2S microphones might not require MCLK from the Teensy, or might provide their own. Check your microphone module's datasheet. If MCLK is not needed by the mic, this pin might not be connected or the Audio Library configured accordingly.)
   - Wiring:
       - Connect the Teensy's I2S BCLK (Pin 21) to the microphone's BCLK/SCK pin.
       - Connect the Teensy's I2S LRCLK (Pin 20) to the microphone's LRCLK/WS pin.
       - Connect the Teensy's I2S DIN (Pin 8) to the microphone's DOUT/SD_OUT pin.
       - If the microphone requires MCLK, connect Teensy's MCLK (Pin 11) to the microphone's MCLK pin.
       - Connect the microphone's VCC to 3.3V from the Teensy.
       - Connect the microphone's GND to Teensy's GND.

**3. Optical SPDIF Input:**
   - Purpose: Receive digital audio via optical connection.
   - Interface: Requires an external SPDIF (Optical/TOSLINK) to I2S converter module (e.g., based on WM8804, CS8416, DIR9001 or similar chips). The Teensy itself does not directly accept raw optical SPDIF.
   - Teensy Pins (for the I2S output from the converter module):
       - This example assumes the SPDIF converter module outputs I2S and it's connected to a secondary I2S interface on the Teensy (if available and configured, e.g., `I2S2`) or carefully managed with the primary I2S pins if the microphone is not used simultaneously or an I2S multiplexer is employed. For this example, we'll list common pins for a secondary I2S input functionality, though specific Teensy models and Audio Library configurations vary.
       - BCLK_SPDIF: Pin 9 (Example: I2S1_TX_BCLK, may need pinmux for RX)
       - LRCLK_SPDIF: Pin 23 (Example: I2S1_MCLK or I2S1_TX_FS, may need pinmux for RX)
       - DIN_SPDIF: Pin 7 (Example: I2S1_TX_DATA, may need pinmux for RX)
   - Wiring:
       - Connect the SPDIF optical receiver module's VCC to an appropriate power source (typically 3.3V or 5V, check module specs) and GND to Teensy's GND.
       - Connect the I2S BCLK output from the SPDIF converter module to Teensy's BCLK_SPDIF (e.g., Pin 9).
       - Connect the I2S LRCLK/WS output from the SPDIF converter module to Teensy's LRCLK_SPDIF (e.g., Pin 23).
       - Connect the I2S Data output from the SPDIF converter module to Teensy's DIN_SPDIF (e.g., Pin 7).
   - Critical Note: The SPDIF input requires a dedicated converter. The Teensy Audio Library's `AudioInputSPDIF` object might be available for some Teensy versions with direct SPDIF electrical (not optical) input on certain pins, but optical input always needs external conversion first. The description above assumes conversion to I2S.

### ESP8266 Peripheral Connections (Example: Wemos D1 Mini / NodeMCU)

Power and ground for these peripherals should be made to the ESP8266's 3.3V output and GND pins.

**1. LCD Screen (I2C):**
   - Purpose: Display information and UI elements.
   - Interface: I2C (assuming a common LCD module with an I2C backpack, e.g., based on PCF8574).
   - ESP8266 Pins:
       - SDA: GPIO4 (D2 on Wemos/NodeMCU)
       - SCL: GPIO5 (D1 on Wemos/NodeMCU)
   - Wiring:
       - Connect ESP8266's GPIO4 (D2) to the LCD module's SDA pin.
       - Connect ESP8266's GPIO5 (D1) to the LCD module's SCL pin.
       - Connect the LCD module's VCC to ESP8266's 3.3V (or 5V if the module has its own regulator and is 5V tolerant, check datasheet).
       - Connect the LCD module's GND to ESP8266's GND.
   - Pull-up Resistors: I2C LCD backpacks usually include pull-up resistors. If not, or if issues arise, add 4.7kΩ pull-ups on SDA and SCL to 3.3V.

**2. Front Button (Digital Input):**
   - Purpose: User input.
   - Interface: Digital Input with an external pull-down resistor.
   - ESP8266 Pin:
       - INPUT_PIN: GPIO12 (D6 on Wemos/NodeMCU)
   - Wiring:
       - Connect one terminal of the push button to ESP8266's GPIO12 (D6).
       - Connect the other terminal of the push button to ESP8266's 3.3V output.
       - Connect a 10kΩ pull-down resistor between GPIO12 (D6) and ESP8266's GND. (When the button is pressed, GPIO12 will read HIGH; otherwise, it will read LOW).

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
* PUT /calibrate/{spl}
  * spl: 40-120 The SPL measured at the listening position. When saved, it is saved with the corresponding SPL measured from the internal mic.
* PUT /sub/{state}
  * state: "on" or "off"
* PUT /bypass/{state}
  * state: "on" or "off"
* PUT /mute/{state}
  * state: "on" or "off"
* PUT /mute/percent/{percent}
  * percent: 1-100
* PUT /generate/tone/{freq}/{volume}
  * freq: 10-20k
  * volume: 1-100
* PUT /generate/noise/{volume}
  * volume: 0-100 A zero value turns off the noise
* PUT /pulse
  * Plays a 100hz pulse for 200ms on each output, with a 300ms silence inbetween
* PUT /preset/rename/{name}/{new}
  * name: string matching existing preset
  * new: new preset name
* POST /preset/create/{name}
  * name: string not matching existing preset
* POST /preset/copy/{name}/{new}
  * name: string matching existing preset
  * new: new preset name
* DEL /preset/{name}
  * name: string matching existing preset
* PUT /preset/delay/{speaker}/{ms}
  * speaker: "left", "right", "sub"
  * ms: float, millisecond value
* POST /preset/{name}/eq/{type}/{spl}
  * type: "room" or "pref"
  * name: string matching existing preset
  * spl: 0-120
  * body: JSON representation of PEQ set
* DEL /preset/{name}/eq/{type}/{spl}
  * type: "room" or "pref"
  * name: string matching existing preset
  * spl: 0-120 matching existing spl for this preset
* PUT /preset/{name}/crossover/{freq}/slope
  * name: string matching existing preset
  * freq: 40-150
  * slope: "12" or "24"
* PUT /preset/{name}/equal-loudness/{state}
  * name: string matching existing preset
  * state: "on" or "off"
* GET /presets
  * Returns a JSON array of presets, where each preset object contains
    * name
    * isCurrent: bool
* GET /preset/{name}
  * Returns JSON of all preset data
* SOCKET /live-updates
  * Messages to be received are JSON format with the following properties:
    * event: "rta", "preset", "eq"
