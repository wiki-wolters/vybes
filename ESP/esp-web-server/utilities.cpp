#include "utilities.h"
#include "globals.h"
#include "config.h"
#include <ESPmDNS.h>

bool startMdns() {
    // end() is a no-op when the responder isn't running, so this doubles as
    // a restart for device-name changes
    MDNS.end();
    if (!MDNS.begin(current_config.deviceName)) {
        DebugSerial.println("mDNS responder failed to start");
        return false;
    }
    MDNS.addService("http", "tcp", 80);
#ifdef CONFIG_IDF_TARGET_ESP32S3
    MDNS.addService("https", "tcp", 443);
#endif
    DebugSerial.printf("mDNS responder started: %s.local\n", current_config.deviceName);
    return true;
}

unsigned char h2int(char c) {
    if (c >= '0' && c <= '9') {
        return ((unsigned char)c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return ((unsigned char)c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return ((unsigned char)c - 'A' + 10);
    }
    return (0);
}

String urlDecode(String str) {
    String decodedString = "";
    char c;
    char code0;
    char code1;
    for (unsigned int i = 0; i < str.length(); i++) {
        c = str.charAt(i);
        if (c == '+') {
            decodedString += ' ';
        } else if (c == '%') {
            i++;
            code0 = str.charAt(i);
            i++;
            code1 = str.charAt(i);
            c = (h2int(code0) << 4) | h2int(code1);
            decodedString += c;
        } else {
            decodedString += c;
        }
    }
    return decodedString;
}
