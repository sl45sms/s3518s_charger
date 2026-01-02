#!/bin/bash

# Full flash erase helper for ESP32-C6.
# Reads PORT from .env if present, otherwise auto-detects a USB serial device.

set -e

# read .env file and export variables (optional)
if [ -f .env ]; then
	set -a
	source .env
	set +a
fi

# Use PORT from .env; if unset, auto-detect a likely serial device.
PORT=${PORT:-$(ls /dev/cu.* 2>/dev/null | egrep 'usb(serial|modem)' | head -n 1)}
if [ -z "$PORT" ]; then
	echo "No serial PORT found. Set PORT in .env (e.g. PORT=/dev/cu.usbserial-XXXX)" >&2
	exit 1
fi

ESPTOOL_BASE="$HOME/Library/Arduino15/packages/esp32/tools/esptool_py"
ESPTOOL=""
if [ -d "$ESPTOOL_BASE" ]; then
	ESPTOOL="$(ls -d "$ESPTOOL_BASE"/* 2>/dev/null | sort -V | tail -n 1)/esptool"
fi

if [ -z "$ESPTOOL" ] || [ ! -x "$ESPTOOL" ]; then
	echo "esptool not found under $ESPTOOL_BASE" >&2
	echo "Install/update esp32 core via Arduino IDE/arduino-cli, then retry." >&2
	exit 1
fi

echo "Erasing full flash on ESP32-C6 via $PORT ..."
echo "(This will remove firmware, partitions, SPIFFS content, etc.)"
"$ESPTOOL" --chip esp32c6 --port "$PORT" --baud 921600 erase_flash

echo "Done. You can now run ./build.sh to flash firmware + SPIFFS." 
