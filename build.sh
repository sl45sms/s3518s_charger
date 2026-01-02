#!/bin/bash
#read .env file and export variables
set -a
source .env
set +a

# Use PORT from .env; if unset, auto-detect the first /dev/cu.usbserial-* device (adjust as needed).
PORT=${PORT:-$(ls /dev/cu.* | grep usbserial | head -n 1)}
# set the correct fqbn for your board, default is for esp32c6
FQBN=${FQBN:-esp32:esp32:esp32c6}
# build directory so sketch + FS image share artifacts
BUILD_DIR=${BUILD_DIR:-build}


#use arduino cli to build the project
arduino-cli compile --fqbn $FQBN \
	--build-path "$BUILD_DIR" \
	--build-property compiler.cpp.extra_flags="-DYOUR_WIFI_PASSWORD=\"$YOUR_WIFI_PASSWORD\"  -DYOUR_WIFI_SSID=\"$YOUR_WIFI_SSID\"" \
	--no-color --verbose s3518s_charger.ino

# upload SPIFFS contents from ./data using mkspiffs + esptool (custom partition offset 0x290000 size 0x160000)
FS_IMAGE="$BUILD_DIR/spiffs.bin"
MKFS_BASE="$HOME/Library/Arduino15/packages/esp32/tools/mkspiffs"
MKFS_TOOL=""
if [ -d "$MKFS_BASE" ]; then
	MKFS_TOOL="$(ls -d "$MKFS_BASE"/* 2>/dev/null | sort -V | tail -n 1)/mkspiffs"
fi

ESPTOOL_BASE="$HOME/Library/Arduino15/packages/esp32/tools/esptool_py"
ESPTOOL=""
if [ -d "$ESPTOOL_BASE" ]; then
	ESPTOOL="$(ls -d "$ESPTOOL_BASE"/* 2>/dev/null | sort -V | tail -n 1)/esptool"
fi

FS_OFFSET=0x290000

if [ -n "$MKFS_TOOL" ] && [ -x "$MKFS_TOOL" ]; then
	"$MKFS_TOOL" -c data -b 4096 -p 256 -s 0x160000 "$FS_IMAGE"
	if [ -f "$FS_IMAGE" ]; then
		if [ -n "$ESPTOOL" ] && [ -x "$ESPTOOL" ]; then
			"$ESPTOOL" --chip esp32c6 --port "$PORT" --baud 921600 write_flash "$FS_OFFSET" "$FS_IMAGE"
		else
			echo "esptool not found under $ESPTOOL_BASE; skipping FS upload" >&2
		fi
	else
		echo "SPIFFS image not created; skipping FS upload" >&2
	fi
else
	echo "mkspiffs not found under $MKFS_BASE; skipping FS image upload (firmware will serve embedded /index.html)" >&2
fi



# upload the sketch
arduino-cli upload -p $PORT --fqbn $FQBN --input-dir "$BUILD_DIR"

# monitor the serial output
arduino-cli monitor -p $PORT --config 115200
