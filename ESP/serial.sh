#!/bin/bash

# Script to connect to ESP32 serial and log output

# Default baud rate
BAUD_RATE=115200

# Find available serial ports
SERIAL_PORTS=($(ls /dev/cu.* 2>/dev/null))

if [ ${#SERIAL_PORTS[@]} -eq 0 ]; then
    echo "No serial ports found. Please ensure your ESP32 is connected."
    exit 1
fi

echo "Available serial ports:"
select SELECTED_PORT in "${SERIAL_PORTS[@]}"; do
    if [ -n "$SELECTED_PORT" ]; then
        break
    else
        echo "Invalid selection. Please try again."
    fi
done

echo "Connecting to $SELECTED_PORT at $BAUD_RATE baud..."
echo "Logging session to 'screenlog.0' in the current directory."
echo "Press Ctrl+A then K to quit screen, then press 'y' to confirm."
echo "-----------------------------------------------------------------"

# Connect using screen and enable logging (-L)
screen -L "$SELECTED_PORT" "$BAUD_RATE"

echo "-----------------------------------------------------------------"
echo "Session ended. Log saved to 'screenlog.0'."
echo "You can view the log using: less screenlog.0"
