#!/bin/bash
#read .env file and export variables
set -a
source .env
set +a

# Use PORT from .env; if unset, auto-detect the first /dev/cu.usbserial-* device (adjust as needed).
PORT=${PORT:-$(ls /dev/cu.* | grep usbserial | head -n 1)}
# set the correct fqbn for your board, default is for esp32c6
FQBN=${FQBN:-esp32:esp32:esp32c6}


#use arduino cli to build the project
arduino-cli compile --fqbn $FQBN --build-property compiler.cpp.extra_flags="-DYOUR_WIFI_PASSWORD=\"$YOUR_WIFI_PASSWORD\"  -DYOUR_WIFI_SSID=\"$YOUR_WIFI_SSID\""  --no-color --verbose s3518s_charger.ino
#upload the project to the board
arduino-cli upload -p $PORT --fqbn $FQBN s3518s_charger.ino
#monitor the serial output
arduino-cli monitor -p $PORT --config 115200
