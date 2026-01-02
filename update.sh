#!/bin/bash

# Update only the SPIFFS filesystem contents (./data) on the board.
# Assumes the firmware + partition table already define a SPIFFS partition.

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

# Tool detection (version-agnostic under Arduino15).
MKFS_BASE="$HOME/Library/Arduino15/packages/esp32/tools/mkspiffs"
MKFS_TOOL=""
if [ -d "$MKFS_BASE" ]; then
	MKFS_TOOL="$(ls -d "$MKFS_BASE"/* 2>/dev/null | sort -V | tail -n 1)/mkspiffs"
fi
if [ -z "$MKFS_TOOL" ] || [ ! -x "$MKFS_TOOL" ]; then
	echo "mkspiffs not found under $MKFS_BASE" >&2
	echo "Install/update esp32 core via Arduino IDE/arduino-cli, then retry." >&2
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

# Derive SPIFFS offset + size from partitions.csv when possible.
FS_OFFSET=""
FS_SIZE=""
if [ -f partitions.csv ]; then
	read -r FS_OFFSET FS_SIZE < <(awk -F',' '
		/^[[:space:]]*spiffs[[:space:]]*,/ {
			o=$4; s=$5;
			gsub(/[[:space:]]/,"",o);
			gsub(/[[:space:]]/,"",s);
			print o, s;
			exit;
		}
	' partitions.csv)
fi

# Fallbacks (must match partitions.csv in this repo)
FS_OFFSET=${FS_OFFSET:-0x290000}
FS_SIZE=${FS_SIZE:-0x160000}

# Tunables
CHIP=${CHIP:-esp32c6}
BAUD=${BAUD:-921600}
BUILD_DIR=${BUILD_DIR:-build}

FS_IMAGE="$BUILD_DIR/spiffs.bin"
mkdir -p "$BUILD_DIR"

if [ ! -d data ]; then
	echo "Missing ./data folder; nothing to upload" >&2
	exit 1
fi

echo "Building SPIFFS image from ./data (size=$FS_SIZE) ..."
"$MKFS_TOOL" -c data -b 4096 -p 256 -s "$FS_SIZE" "$FS_IMAGE"

if [ ! -f "$FS_IMAGE" ]; then
	echo "SPIFFS image not created at $FS_IMAGE" >&2
	exit 1
fi

echo "Flashing SPIFFS image to offset $FS_OFFSET on $CHIP via $PORT ..."
"$ESPTOOL" --chip "$CHIP" --port "$PORT" --baud "$BAUD" write_flash "$FS_OFFSET" "$FS_IMAGE"

echo "Done. Refresh the web UI in your browser." 
