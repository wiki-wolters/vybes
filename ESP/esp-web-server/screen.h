#ifndef SCREEN_H
#define SCREEN_H

#include <Arduino.h>

void setupScreen();
void writeToScreen(String message, unsigned long duration = 0);
void loopScreen();

#endif