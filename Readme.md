A custom charger with display, so now you can monitor what's going on while charging your devices.

It has a display to show voltage, and current with oscilloscope-like graphs.

Also have a webserver to remotely monitor the charger status, with graphs and data logging in browser local storage.

# build instructions
1. Install Arduino IDE and arduino-cli.
2. Install the required h1_SW35xx library:
   - download as zip from https://github.com/happyme531/h1_SW35xx 
   - Go to Sketch -> Include Library -> Manage Libraries...
    - Click on the gear icon on the top right and select "Add .ZIP Library..."
    
3. create a .env file in the root of the project with your WiFi credentials:
   - YOUR_WIFI_SSID=your_wifi_ssid
   - YOUR_WIFI_PASSWORD=your_wifi_password

4. use arduino-cli via build.sh to build and upload to your board.

Note: The web UI is served from SPIFFS. The contents of ./data (including data/index.html) are packed into a SPIFFS image and flashed by build.sh.

# hardware
The charger is based on the SS3518S chip from Silan Microelectronics.
It supports various battery chemistries and has built-in safety features.
It can be controlled via I2C interface, which is used in this project to monitor and control the charging process.
   