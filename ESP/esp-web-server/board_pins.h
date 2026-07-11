#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include <sdkconfig.h>

// Per-board GPIO assignments, selected by the chip the env builds for
// (ESP/platformio.ini: env:esp32dev / env:esp32s3). Wiring tables and the
// reasoning behind each choice live in docs/WIRING.md. To support another
// ESP32 variant, add a block here and an env in platformio.ini.

#if defined(CONFIG_IDF_TARGET_ESP32S3)

// ESP32-S3 DevKitC-1. Avoids strapping pins (0, 3, 45, 46), native USB
// (19/20), flash (26-32) and octal-PSRAM (33-37) pins.
#define PIN_TEENSY_RX   16  // UART2 RX, from Teensy TX1 (pin 1)
#define PIN_TEENSY_TX   17  // UART2 TX, to Teensy RX1 (pin 0)
#define PIN_I2C_SDA     8   // LCD backpack SDA
#define PIN_I2C_SCL     9   // LCD backpack SCL
#define PIN_IR_RECV     4   // IR receiver data out
#define PIN_BUTTON      5   // Front button to GND (internal pull-up)
#define PIN_BT_PAIRING  6   // Output to the Bluetooth module's pair pin

#elif defined(CONFIG_IDF_TARGET_ESP32)

// Classic ESP32 DevKitC (WROOM-32). Avoids strapping pins (0, 2, 5, 12, 15)
// and the input-only, no-pull-up range (34-39).
#define PIN_TEENSY_RX   16  // UART2 RX, from Teensy TX1 (pin 1)
#define PIN_TEENSY_TX   17  // UART2 TX, to Teensy RX1 (pin 0)
#define PIN_I2C_SDA     21  // LCD backpack SDA
#define PIN_I2C_SCL     22  // LCD backpack SCL
#define PIN_IR_RECV     4   // IR receiver data out
#define PIN_BUTTON      32  // Front button to GND (internal pull-up)
#define PIN_BT_PAIRING  33  // Output to the Bluetooth module's pair pin

#else
#error "Unsupported target - add a pin map for this chip in board_pins.h"
#endif

#endif // BOARD_PINS_H
