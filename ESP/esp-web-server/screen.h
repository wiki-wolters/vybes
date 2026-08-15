#ifndef SCREEN_H
#define SCREEN_H

#include <Arduino.h>
#include <LiquidCrystal_PCF8574.h>

extern unsigned long backlightStart;
extern LiquidCrystal_PCF8574 lcd;

void setupScreen();
void writeToScreen(String message, unsigned long duration = 0);
// Call once setup() is done: see screen.cpp for why the timer cannot simply
// run from the boot-time write.
void restartBacklightTimer();
void loopScreen();

#endif