#ifndef SCREEN_H
#define SCREEN_H

#include <Arduino.h>
#include <LiquidCrystal_PCF8574.h>

extern unsigned long backlightStart;
extern LiquidCrystal_PCF8574 lcd;

void setupScreen();
void writeToScreen(String message, unsigned long duration = 0);
void loopScreen();

#endif