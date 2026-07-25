#ifndef UTILITIES_H
#define UTILITIES_H

#include <Arduino.h>

String urlDecode(String str);

void scheduleConfigWrite();

// (Re)announce "<deviceName>.local" over mDNS with the http/https services.
// Called at boot and again whenever the device name changes. Returns false
// when the responder failed to start.
bool startMdns();

#endif
