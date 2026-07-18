#include "SerialCommandRouter.h"

SerialCommandRouter::SerialCommandRouter(HardwareSerial& port)
    : port(port), output(port), commandCount(0), lineLength(0), lineOverflow(false) {
}

void SerialCommandRouter::begin(uint32_t baud) {
    port.begin(baud);
}

void SerialCommandRouter::on(const String& commandName, Handler handler) {
    if (commandCount < MAX_COMMANDS) {
        commands[commandCount].name = commandName;
        commands[commandCount].handler = handler;
        commandCount++;
    } else {
        Serial.println("Error: Maximum commands reached");
    }
}

void SerialCommandRouter::sendEvent(const char* name) {
    port.print("EVENT ");
    port.println(name);
}

void SerialCommandRouter::loop() {
    while (port.available()) {
        char c = port.read();
        if (c == '\r') continue;
        if (c == '\n') {
            lineBuffer[lineLength] = '\0';
            if (lineOverflow) {
                Serial.println("Serial command too long - dropped");
            } else if (lineLength > 0) {
                String command(lineBuffer);
                command.trim();
                if (command.length() > 0) {
                    processCommand(command, output);
                }
            }
            lineLength = 0;
            lineOverflow = false;
        } else if (lineLength < LINE_BUFFER_SIZE - 1) {
            lineBuffer[lineLength++] = c;
        } else {
            lineOverflow = true;
        }
    }
}

void SerialCommandRouter::processCommand(const String& rawCommand, OutputStream& out) {
    String cmd_str;
    String argsString;

    int firstDelim = rawCommand.indexOf(' ');
    if (firstDelim == -1) {
        cmd_str = rawCommand;
        argsString = "";
    } else {
        cmd_str = rawCommand.substring(0, firstDelim);
        argsString = rawCommand.substring(firstDelim + 1);
    }

    cmd_str.trim();
    argsString.trim();

    // Find matching command
    for (int i = 0; i < commandCount; i++) {
        if (commands[i].name.equalsIgnoreCase(cmd_str)) {
            int argCount;
            String* args = parseArgs(argsString, argCount);

            commands[i].handler(cmd_str, args, argCount, out);

            if (argCount > 0 && args != nullptr) {
                delete[] args;
            }
            return;
        }
    }

    Serial.print("Command not found: ");
    Serial.println(cmd_str);
}

String* SerialCommandRouter::parseArgs(const String& argsString, int& count) {
    // Count tokens, treating runs of spaces as a single delimiter so
    // repeated spaces don't produce empty arguments
    count = 0;
    bool inToken = false;
    for (unsigned int i = 0; i < argsString.length(); i++) {
        if (argsString.charAt(i) == ' ') {
            inToken = false;
        } else if (!inToken) {
            inToken = true;
            count++;
        }
    }

    if (count == 0) {
        return nullptr;
    }

    String* result = new String[count];
    int index = 0;
    int start = -1;

    for (unsigned int i = 0; i <= argsString.length(); i++) {
        bool isSpace = (i == argsString.length()) || argsString.charAt(i) == ' ';
        if (!isSpace && start < 0) {
            start = i;
        } else if (isSpace && start >= 0) {
            result[index++] = argsString.substring(start, i);
            start = -1;
        }
    }

    return result;
}
