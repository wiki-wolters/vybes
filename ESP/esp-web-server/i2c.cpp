#include <Arduino.h>
#include "globals.h"
#include "i2c.h"
#include <Wire.h>

// I2C now only carries the PCF8574 LCD backpack; the Teensy link is UART.
void initI2C() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000); //Standard
}

void scanI2CBus() {
    byte error, address;
    int nDevices;

    DebugSerial.println("Scanning...");

    nDevices = 0;
    for(address = 1; address < 127; address++ ) {
        // The i2c_scanner uses the return value of
        // the Write.endTransmisstion to see if
        // a device did acknowledge to the address.
        Wire.beginTransmission(address);
        error = Wire.endTransmission();

        if (error == 0) {
        DebugSerial.print("I2C device found at address 0x");
        if (address<16)
            DebugSerial.print("0");
            DebugSerial.print(address,HEX);
            DebugSerial.println(" !");

            nDevices++;
        }
        else if (error==4) {
            DebugSerial.print("Unknown error at address 0x");
            if (address<16)
                DebugSerial.print("0");
            DebugSerial.println(address,HEX);
        }
    }
    if (nDevices == 0)
        DebugSerial.println("No I2C devices found\n");
    else
        DebugSerial.println("done\n");
}
