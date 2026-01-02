#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Wire.h>
#include <h1_SW35xx.h>
using namespace h1_SW35xx;

// Some Adafruit_ST77xx versions don't define ST77XX_DARKGREY.
// Fall back to a close color.
#ifndef ST77XX_DARKGREY
#define ST77XX_DARKGREY 0x7BEF
#endif

// --- Display parameters ---
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240

// --- Pin mapping to dev board ---
#define TFT_DC   11 //board pin 11
#define TFT_RST  10 //board pin 10
#define TFT_MOSI 7  //board pin 7 
#define TFT_MISO 2  //board pin 3, not used
#define TFT_SCLK  6 //board pin 6
#define TFT_CS   -1 //CS-less / tied low on the module
#define TFT_BLK  3  //board pin 3,  Display backlight pin

// Sould use SPI mode 3 for this panel.
#define TFT_SPI_MODE SPI_MODE3
// Set to 80MHz maximum for ESP32-C6
#define TFT_SPI_HZ   80000000

// Backlight is active-high.
#define TFT_BLK_ON   1
#define TFT_BLK_OFF  (!TFT_BLK_ON)

// Use the core-provided global SPI instance on ESP32-C6 (tends to be the most reliable).
Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);

static void backlight(bool on) {
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, on ? TFT_BLK_ON : TFT_BLK_OFF);
}

// ESP32-C6 Default I2C Pins, on board is pins 5,6!
#define I2C_SDA 23
#define I2C_SCL 20

SW35xx sw(Wire);

// --- Non-blocking web server (ESP32 only) ---
#ifndef ARDUINO_ARCH_ESP32
#define ENABLE_WEBSERVER 0
#else
#define ENABLE_WEBSERVER 1
#endif



#if ENABLE_WEBSERVER
#ifndef YOUR_WIFI_SSID
#define YOUR_WIFI_SSID ""
#endif
#ifndef YOUR_WIFI_PASSWORD
#define YOUR_WIFI_PASSWORD ""
#endif

#include <WiFi.h>
#include <WebServer.h>

  // Do NOT commit real secrets. Put your SSID/PASS here locally.
  static constexpr const char* WIFI_SSID = YOUR_WIFI_SSID;
  static constexpr const char* WIFI_PASS = YOUR_WIFI_PASSWORD;
  static constexpr uint16_t WEB_PORT = 80;

  static WebServer server(WEB_PORT);

  struct CachedStatus {
    uint32_t ms;
    int vin_mV;
    int vout_mV;
    int iout_usbc_mA;
    int iout_usba_mA;
    int pdVersion;
    SW35xx::fastChargeType_t fastChargeType;
  };

  static CachedStatus gStatus = {0};

  enum class WifiState : uint8_t { Idle, Connecting, Connected };
  static WifiState gWifiState = WifiState::Idle;
  static uint32_t gNextWifiActionMs = 0;
  static bool gLoggedMissingWifi = false;

  static void updateCachedStatus(uint32_t nowMs) {
    // Copy from SW35xx object once per read cycle; handlers only read gStatus.
    gStatus.ms = nowMs;
    gStatus.vin_mV = sw.vin_mV;
    gStatus.vout_mV = sw.vout_mV;
    gStatus.iout_usbc_mA = sw.iout_usbc_mA;
    gStatus.iout_usba_mA = sw.iout_usba_mA;
    gStatus.fastChargeType = sw.fastChargeType;
    gStatus.pdVersion = sw.PDVersion;
  }

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>SW3518 Monitor</title>
  <style>
    body{font-family:system-ui,Segoe UI,Arial,sans-serif;margin:16px;max-width:720px}
    .row{display:flex;gap:12px;flex-wrap:wrap}
    .card{border:1px solid #ddd;border-radius:8px;padding:12px;min-width:200px}
    .k{color:#666;font-size:12px}
    .v{font-size:22px;font-weight:600}
    code{background:#f6f6f6;padding:2px 6px;border-radius:6px}
  </style>
</head>
<body>
  <h2>SW3518 Status</h2>
  <div class="row">
    <div class="card"><div class="k">VIN</div><div class="v" id="vin">-</div></div>
    <div class="card"><div class="k">VOUT</div><div class="v" id="vout">-</div></div>
    <div class="card"><div class="k">USB-C</div><div class="v" id="usbc">-</div></div>
    <div class="card"><div class="k">USB-A</div><div class="v" id="usba">-</div></div>
    <div class="card"><div class="k">Type</div><div class="v" id="type">-</div></div>
    <div class="card"><div class="k">PD</div><div class="v" id="pd">-</div></div>
  </div>
  <p>API: <code>/api/status</code></p>
  <script>
    async function tick(){
      try{
        const r = await fetch('/api/status', {cache:'no-store'});
        if(!r.ok) throw new Error(r.status);
        const j = await r.json();
        document.getElementById('vin').textContent  = j.vin_mV + ' mV';
        document.getElementById('vout').textContent = j.vout_mV + ' mV';
        document.getElementById('usbc').textContent = j.iout_usbc_mA + ' mA';
        document.getElementById('usba').textContent = j.iout_usba_mA + ' mA';
        document.getElementById('type').textContent = j.fastChargeType;
        document.getElementById('pd').textContent   = j.pdVersion ? ('v' + j.pdVersion) : '-';
      }catch(e){
        // keep last values
      }
      setTimeout(tick, 500);
    }
    tick();
  </script>
</body>
</html>
)HTML";

  static void handleRoot() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  }

  // Forward declare fastChargeType2String so it can be used here
  const char *fastChargeType2String(SW35xx::fastChargeType_t fastChargeType);

  static void handleStatus() {
    // O(1), cached-only, no I2C, no loops, no delay.
    char json[256];
    const char* typeStr = fastChargeType2String(gStatus.fastChargeType);
    const uint16_t total_mA = (uint16_t)(gStatus.iout_usbc_mA + gStatus.iout_usba_mA);

    // Keep it simple (no ArduinoJson dependency).
    snprintf(json, sizeof(json),
      "{\"ms\":%lu,"
      "\"vin_mV\":%d,\"vout_mV\":%d,"
      "\"iout_usbc_mA\":%d,\"iout_usba_mA\":%d,\"iout_total_mA\":%u,"
      "\"fastChargeType\":\"%s\",\"pdVersion\":%d}",
      (unsigned long)gStatus.ms,
      gStatus.vin_mV, gStatus.vout_mV,
      gStatus.iout_usbc_mA, gStatus.iout_usba_mA, (unsigned)total_mA,
      typeStr, gStatus.pdVersion
    );

    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json; charset=utf-8", json);
  }

  static void webSetup() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.onNotFound([]() { server.send(404, "text/plain; charset=utf-8", "Not found"); });
  }

  static void wifiLoop(uint32_t now) {
    if (WIFI_SSID[0] == '\0' || WIFI_PASS[0] == '\0') {
      if (!gLoggedMissingWifi) {
        gLoggedMissingWifi = true;
        Serial.println("WiFi disabled: set YOUR_WIFI_SSID/YOUR_WIFI_PASSWORD (see build.sh .env)");
      }
      return; // placeholders not set
    }

    const wl_status_t st = WiFi.status();

    if (st == WL_CONNECT_FAILED) {
      gWifiState = WifiState::Idle;
      gNextWifiActionMs = now + 5000;
      WiFi.disconnect(true /*wifioff*/);
      Serial.println("WiFi connect failed; retrying in 5s");
      return;
    }

    if (st == WL_CONNECTED) {
      if (gWifiState != WifiState::Connected) {
        gWifiState = WifiState::Connected;
        Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
        server.begin();
        Serial.printf("HTTP server started on port %u\n", (unsigned)WEB_PORT);
      }
      return;
    }

    // Not connected.
    if (now < gNextWifiActionMs) return;

    // Retry every 10s while disconnected.
    gNextWifiActionMs = now + 10000;

    if (gWifiState != WifiState::Connecting) {
      gWifiState = WifiState::Connecting;
      WiFi.persistent(false);
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(true);
      WiFi.setAutoReconnect(true);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      Serial.printf("WiFi connecting to %s...\n", WIFI_SSID);
    } else {
      // still connecting; allow retries by restarting begin()
      WiFi.disconnect(false /*wifioff*/);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      Serial.println("WiFi retry...");
    }
  }

  static void webLoop(uint32_t now) {
    (void)now;
    wifiLoop(now);
    if (WiFi.status() == WL_CONNECTED) {
      server.handleClient(); // must be called frequently; handlers are O(1)
    }
  }
#else
  // Stubs to keep non-ESP32 builds compiling.
  static inline void updateCachedStatus(uint32_t) {}
  static inline void webSetup() {}
  static inline void webLoop(uint32_t) {}
#endif

// --- Display layout ---
#define UI_TEXT_SIZE 2
#define UI_LINE_HEIGHT 8 * UI_TEXT_SIZE + 2 // classic 6x8 font
#define UI_TEXT_LINES 8                     // lines 0..7 used today
#define SCOPE_Y UI_TEXT_LINES * UI_LINE_HEIGHT
#define SCOPE_H DISPLAY_HEIGHT - SCOPE_Y
#define SCOPE_W DISPLAY_WIDTH

// --- Optional off-screen scope rendering to reduce flicker ---
// A full 240x240 16-bit framebuffer is ~115KB, but the scope region is smaller.
// With current layout, scope is ~240x96 (~46KB), typically OK on ESP32-C6.
static GFXcanvas16 *scopeCanvas = nullptr;

// --- Optional off-screen text line rendering to reduce flicker ---
// One line buffer: 240 x UI_LINE_HEIGHT x 2 bytes (~8.6KB with current layout).
static GFXcanvas16 *textLineCanvas = nullptr;

// --- Oscilloscope buffers (one sample per loop) ---
static uint16_t voutHistory_mV[SCOPE_W];
static uint16_t ioutHistory_mA[SCOPE_W];
static uint16_t scopeWriteIndex = 0;
static bool scopeInitialized = false;

static inline int16_t mapToScopeY(uint16_t value, uint16_t minValue, uint16_t maxValue, int16_t yTop, int16_t height) {
  if (height <= 1) return yTop;
  uint16_t range = (maxValue > minValue) ? (maxValue - minValue) : 1;
  uint16_t clamped = value;
  if (clamped < minValue) clamped = minValue;
  if (clamped > maxValue) clamped = maxValue;
  // Higher value should be higher on screen (smaller y).
  int32_t scaled = (int32_t)(clamped - minValue) * (height - 1) / range;
  return (int16_t)(yTop + (height - 1) - scaled);
}

static void drawOscilloscope(uint16_t vout_mV, uint16_t iout_total_mA) {
  if (SCOPE_H <= 12) return; // need room for 2 lanes + borders

  if (!scopeInitialized) {
    for (int16_t i = 0; i < SCOPE_W; i++) {
      voutHistory_mV[i] = vout_mV;
      ioutHistory_mA[i] = iout_total_mA;
    }
    scopeWriteIndex = 0;
    scopeInitialized = true;
  }

  // Ring buffer: after increment, scopeWriteIndex points at the oldest sample.
  voutHistory_mV[scopeWriteIndex] = vout_mV;
  ioutHistory_mA[scopeWriteIndex] = iout_total_mA;
  scopeWriteIndex = (uint16_t)((scopeWriteIndex + 1) % SCOPE_W);

  // Autoscale from history with small padding.
  uint16_t vMin = 65535, vMax = 0;
  uint16_t iMin = 65535, iMax = 0;
  for (int16_t i = 0; i < SCOPE_W; i++) {
    const uint16_t vv = voutHistory_mV[i];
    const uint16_t ii = ioutHistory_mA[i];
    if (vv < vMin) vMin = vv;
    if (vv > vMax) vMax = vv;
    if (ii < iMin) iMin = ii;
    if (ii > iMax) iMax = ii;
  }

  // Add a little range so flat signals still show.
  if ((uint16_t)(vMax - vMin) < 50) { // 50mV min range
    const uint16_t mid = (uint16_t)(((uint32_t)vMax + (uint32_t)vMin) / 2u);
    vMin = (mid > 25) ? (uint16_t)(mid - 25) : 0;
    vMax = (uint16_t)(mid + 25);
  }
  if ((uint16_t)(iMax - iMin) < 50) { // 50mA min range
    const uint16_t mid = (uint16_t)(((uint32_t)iMax + (uint32_t)iMin) / 2u);
    iMin = (mid > 25) ? (uint16_t)(mid - 25) : 0;
    iMax = (uint16_t)(mid + 25);
  }

  // Smooth the scale so it doesn't "jump" every frame.
  static bool scaleInit = false;
  static uint16_t vMinS, vMaxS, iMinS, iMaxS;
  if (!scaleInit) {
    vMinS = vMin; vMaxS = vMax;
    iMinS = iMin; iMaxS = iMax;
    scaleInit = true;
  } else {
    // 1/8 update step (cheap low-pass).
    vMinS = (uint16_t)(((uint32_t)vMinS * 7u + (uint32_t)vMin) / 8u);
    vMaxS = (uint16_t)(((uint32_t)vMaxS * 7u + (uint32_t)vMax) / 8u);
    iMinS = (uint16_t)(((uint32_t)iMinS * 7u + (uint32_t)iMin) / 8u);
    iMaxS = (uint16_t)(((uint32_t)iMaxS * 7u + (uint32_t)iMax) / 8u);
  }
  if (vMaxS <= vMinS) vMaxS = (uint16_t)(vMinS + 1);
  if (iMaxS <= iMinS) iMaxS = (uint16_t)(iMinS + 1);

  // Split inner plot area into two lanes (top: VOUT, bottom: IOUT).
  const int16_t innerTop = 1;
  const int16_t innerBottom = SCOPE_H - 2;
  const int16_t innerH = innerBottom - innerTop + 1;
  const int16_t dividerY = innerTop + innerH / 2;

  const int16_t vTop = innerTop;
  const int16_t vH = dividerY - innerTop;              // pixels available above divider
  const int16_t iTop = dividerY + 1;
  const int16_t iH = innerBottom - iTop + 1;           // pixels available below divider
  if (vH <= 1 || iH <= 1) return;

  auto renderScope = [&](auto &gfx, int16_t originY) {
    // originY: 0 for canvas, SCOPE_Y for direct TFT draw
    gfx.fillRect(0, originY, SCOPE_W, SCOPE_H, ST77XX_BLACK);
    gfx.drawRect(0, originY, SCOPE_W, SCOPE_H, ST77XX_DARKGREY);

    // Lane divider
    gfx.drawFastHLine(1, originY + dividerY, SCOPE_W - 2, ST77XX_DARKGREY);

    int16_t prevYv = 0, prevYi = 0;
    bool havePrev = false;

    for (int16_t x = 0; x < SCOPE_W; x++) {
      const uint16_t idx = (uint16_t)((scopeWriteIndex + x) % SCOPE_W);
      const uint16_t vv = voutHistory_mV[idx];
      const uint16_t ii = ioutHistory_mA[idx];

      const int16_t yv = mapToScopeY(vv, vMinS, vMaxS, originY + vTop, vH);
      const int16_t yi = mapToScopeY(ii, iMinS, iMaxS, originY + iTop, iH);

      if (havePrev) {
        gfx.drawLine(x - 1, prevYv, x, yv, ST77XX_CYAN);
        gfx.drawLine(x - 1, prevYi, x, yi, ST77XX_YELLOW);
      }
      prevYv = yv;
      prevYi = yi;
      havePrev = true;
    }
  };

  if (scopeCanvas) {
    // Render off-screen (0-based coords), then blit once.
    renderScope(*scopeCanvas, 0);
    tft.drawRGBBitmap(0, SCOPE_Y, scopeCanvas->getBuffer(), SCOPE_W, SCOPE_H);
    return;
  }

  // Fallback: direct render into the TFT (may flicker).
  renderScope(tft, SCOPE_Y);
}

const char *fastChargeType2String(SW35xx::fastChargeType_t fastChargeType) {
  switch (fastChargeType) {
  case SW35xx::NOT_FAST_CHARGE:
    return "Slow charge";
    break;
  case SW35xx::QC2:
    return "QC2.0";
    break;
  case SW35xx::QC3:
    return "QC3.0";
    break;
  case SW35xx::FCP:
    return "FCP";
    break;
  case SW35xx::SCP:
    return "SCP";
    break;
  case SW35xx::PD_FIX:
    return "PD Fix";
    break;
  case SW35xx::PD_PPS:
    return "PD PPS";
    break;
  case SW35xx::MTKPE1:
    return "MTK PE1.1";
    break;
  case SW35xx::MTKPE2:
    return "MTK PE2.0";
    break;
  case SW35xx::LVDC:
    return "LVDC";
    break;
  case SW35xx::SFCP:
    return "SFCP";
    break;
  case SW35xx::AFC:
    return "AFC";
    break;
  default:
    return "unknown";
    break;
  }
}

static void drawStatusToDisplay() {
  // Update only the rows that changed to avoid flicker.
  // Text size is set to 2 in setup(). Adafruit classic font is 6x8 px.
  const uint16_t fg = ST77XX_WHITE;
  const uint16_t bg = ST77XX_BLACK;
  const int16_t lineHeight = UI_LINE_HEIGHT; // +2 for breathing room

  tft.setTextColor(fg, bg);
  tft.setTextWrap(false);

  auto drawLine = [&](uint8_t line, const char *text) {
    static char last[10][40];
    if (line >= 10) return;
    if (strncmp(last[line], text, sizeof(last[line])) == 0) return;
    strncpy(last[line], text, sizeof(last[line]) - 1);
    last[line][sizeof(last[line]) - 1] = '\0';

    int16_t y = line * lineHeight;

    // Prefer off-screen line rendering to avoid visible clear/redraw flicker.
    if (textLineCanvas) {
      textLineCanvas->fillScreen(bg);
      textLineCanvas->setTextWrap(false);
      textLineCanvas->setTextSize(UI_TEXT_SIZE);
      textLineCanvas->setTextColor(fg, bg);
      textLineCanvas->setCursor(0, 0);
      textLineCanvas->print(text);
      tft.drawRGBBitmap(0, y, textLineCanvas->getBuffer(), DISPLAY_WIDTH, lineHeight);
    } else {
      tft.fillRect(0, y, DISPLAY_WIDTH, lineHeight, bg);
      tft.setCursor(0, y);
      tft.print(text);
    }
  };

  char buf[40];
  drawLine(0, "SW3518 Status");
  drawLine(1, "----------------");

  snprintf(buf, sizeof(buf), "VIN : %d mV", sw.vin_mV);
  drawLine(2, buf);

  snprintf(buf, sizeof(buf), "VOUT: %d mV", sw.vout_mV);
  drawLine(3, buf);

  snprintf(buf, sizeof(buf), "USB-C: %d mA", sw.iout_usbc_mA);
  drawLine(4, buf);

  snprintf(buf, sizeof(buf), "USB-A: %d mA", sw.iout_usba_mA);
  drawLine(5, buf);

  snprintf(buf, sizeof(buf), "Type: %s", fastChargeType2String(sw.fastChargeType));
  drawLine(6, buf);

  if (sw.fastChargeType == SW35xx::PD_FIX || sw.fastChargeType == SW35xx::PD_PPS) {
    snprintf(buf, sizeof(buf), "PD v: %d", sw.PDVersion);
    drawLine(7, buf);
  } else {
    drawLine(7, "");
  }

  // Bottom oscilloscope: VOUT (cyan) and total output current (yellow).
  uint16_t total_mA = (uint16_t)(sw.iout_usbc_mA + sw.iout_usba_mA);
  drawOscilloscope((uint16_t)sw.vout_mV, total_mA);
}


void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.setDebugOutput(true);
  Serial.println("\nCustom charger init");
  Serial.flush();
  Serial.println("init display");
  // Force the exact pins used for SPI.
  // (SCK, MISO, MOSI, SS) -> MISO not used, SS not used (CS = -1)
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);
  delay(50);
  // Important: use MODE3 for the module
  tft.init(240, 240, TFT_SPI_MODE);
  // Some cores/libraries reset SPI speed during init(); set it after init to be sure.
  tft.setSPISpeed(TFT_SPI_HZ);
  tft.setRotation(2);
  delay(50);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextWrap(true);
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.println("Initialize...");
  backlight(true);

  if (SCOPE_W > 0 && SCOPE_H > 0) {
    scopeCanvas = new GFXcanvas16(SCOPE_W, SCOPE_H);
    if (!scopeCanvas) {
      Serial.println("WARN: scopeCanvas alloc failed, using direct draw");
    }
  }

  if (DISPLAY_WIDTH > 0 && UI_LINE_HEIGHT > 0) {
    textLineCanvas = new GFXcanvas16(DISPLAY_WIDTH, UI_LINE_HEIGHT);
    if (!textLineCanvas) {
      Serial.println("WARN: textLineCanvas alloc failed, using direct text draw");
    }
  }

  Serial.println("init power module");
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(10000);   // Drop speed to 10kHz
#if defined(ARDUINO_ARCH_ESP32)
  // Avoid getting stuck forever if the I2C bus/device glitches.
  Wire.setTimeout(50);
#endif

  // Workaround for SW3518S VIN reading (h1_SW35xx issue #9):
  // enable VIN ADC reading only after I2C is initialized and stable.
  delay(20);                                                 
  sw.begin();
  delay(20);

  sw.setMaxCurrent5A();
  Serial.println("SW3518 Connected Successfully!");

  // Web server init (non-blocking; WiFi connects in loop()).
  webSetup();

#if ENABLE_WEBSERVER
  // Print MAC address for device identification
  uint8_t macAddr[6];
  WiFi.macAddress(macAddr);
  Serial.print("MAC Address: ");
  for (int i = 0; i < 6; i++) {
    if (macAddr[i] < 0x10) Serial.print("0");
    Serial.print(macAddr[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
#endif
delay(2000);
}

void loop() {
  static constexpr uint32_t READ_STATUS_MS = 200;
  static constexpr uint32_t UI_REFRESH_MS = 200;
  static constexpr uint32_t SERIAL_LOG_MS = 2000;

  // Initialize so the first actions happen immediately.
  static uint32_t lastReadMs = (uint32_t)(0 - READ_STATUS_MS);
  static uint32_t lastUiMs = (uint32_t)(0 - UI_REFRESH_MS);
  static uint32_t lastSerialMs = (uint32_t)(0 - SERIAL_LOG_MS);

  const uint32_t now = millis();

  // Run web loop every iteration to keep it responsive.
  webLoop(now);

  if ((uint32_t)(now - lastReadMs) >= READ_STATUS_MS) {
    lastReadMs = now;
    yield();
    sw.readStatus();
    updateCachedStatus(now);
    yield();
  }

  if ((uint32_t)(now - lastUiMs) >= UI_REFRESH_MS) {
    lastUiMs = now;
    drawStatusToDisplay();
  }

  if ((uint32_t)(now - lastSerialMs) >= SERIAL_LOG_MS) {
    lastSerialMs = now;
    Serial.println("Current:===============================");
    Serial.printf("Input voltage:%dmV\n", sw.vin_mV);
    Serial.printf("Output voltage:%dmV\n", sw.vout_mV);
    Serial.printf("USB-C current:%dmA\r\n", sw.iout_usbc_mA);
    Serial.printf("USB-A current:%dmA\r\n", sw.iout_usba_mA);
    Serial.printf("Fast charge type:%s\n", fastChargeType2String(sw.fastChargeType));
    if (sw.fastChargeType == SW35xx::PD_FIX || sw.fastChargeType == SW35xx::PD_PPS)
      Serial.printf("PD version:%d\n", sw.PDVersion);
    Serial.println("=======================================");
    Serial.println("");
  }

  delay(1);
}