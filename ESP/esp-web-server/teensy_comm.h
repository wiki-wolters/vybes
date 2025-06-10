#ifndef TEENSY_COMM_H
#define TEENSY_COMM_H

#include <Arduino.h>

void sendToTeensy(const String& command, const String& data);

#endif
