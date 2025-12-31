#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Wire.h>
#include <h1_SW35xx.h>
using namespace h1_SW35xx;

// Some Adafruit_ST77xx versions don't define ST77XX_DARKGREY.
// Fall back to a known color token to keep the sketch portable.
#ifndef ST77XX_DARKGREY
#define ST77XX_DARKGREY ST77XX_WHITE
#endif

// Match your ESP-IDF example
#define LCD_HOST SPI2_HOST

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

// You sould use SPI mode 3 for this panel.
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

// --- Display layout ---
static constexpr uint8_t UI_TEXT_SIZE = 2;
static constexpr int16_t UI_LINE_HEIGHT = 8 * UI_TEXT_SIZE + 2; // classic 6x8 font
static constexpr uint8_t UI_TEXT_LINES = 8;                     // lines 0..7 used today
static constexpr int16_t SCOPE_Y = UI_TEXT_LINES * UI_LINE_HEIGHT;
static constexpr int16_t SCOPE_H = DISPLAY_HEIGHT - SCOPE_Y;
static constexpr int16_t SCOPE_W = DISPLAY_WIDTH;

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
  if (SCOPE_H <= 8) return; // not enough space

  if (!scopeInitialized) {
    for (int16_t i = 0; i < SCOPE_W; i++) {
      voutHistory_mV[i] = vout_mV;
      ioutHistory_mA[i] = iout_total_mA;
    }
    scopeWriteIndex = 0;
    scopeInitialized = true;
  }

  voutHistory_mV[scopeWriteIndex] = vout_mV;
  ioutHistory_mA[scopeWriteIndex] = iout_total_mA;
  scopeWriteIndex = (uint16_t)((scopeWriteIndex + 1) % SCOPE_W);

  // Autoscale from history with small padding.
  uint16_t vMin = 65535, vMax = 0;
  uint16_t iMin = 65535, iMax = 0;
  for (int16_t i = 0; i < SCOPE_W; i++) {
    uint16_t vv = voutHistory_mV[i];
    uint16_t ii = ioutHistory_mA[i];
    if (vv < vMin) vMin = vv;
    if (vv > vMax) vMax = vv;
    if (ii < iMin) iMin = ii;
    if (ii > iMax) iMax = ii;
  }
  // Add a little range so flat signals still show.
  if (vMax - vMin < 50) { // 50mV min range
    uint16_t mid = (uint16_t)((vMax + vMin) / 2);
    vMin = (mid > 25) ? (uint16_t)(mid - 25) : 0;
    vMax = (uint16_t)(mid + 25);
  }
  if (iMax - iMin < 50) { // 50mA min range
    uint16_t mid = (uint16_t)((iMax + iMin) / 2);
    iMin = (mid > 25) ? (uint16_t)(mid - 25) : 0;
    iMax = (uint16_t)(mid + 25);
  }

  // Clear and draw a simple scope frame.
  tft.fillRect(0, SCOPE_Y, SCOPE_W, SCOPE_H, ST77XX_BLACK);
  tft.drawRect(0, SCOPE_Y, SCOPE_W, SCOPE_H, ST77XX_DARKGREY);
  // Midline for quick reference.
  tft.drawFastHLine(1, SCOPE_Y + SCOPE_H / 2, SCOPE_W - 2, ST77XX_DARKGREY);

  // Plot oldest->newest left->right.
  const int16_t plotTop = SCOPE_Y + 1;
  const int16_t plotH = SCOPE_H - 2;
  if (plotH <= 1) return;

  int16_t prevYv = 0;
  int16_t prevYi = 0;
  bool havePrev = false;

  for (int16_t x = 0; x < SCOPE_W; x++) {
    uint16_t idx = (uint16_t)((scopeWriteIndex + x) % SCOPE_W);
    uint16_t vv = voutHistory_mV[idx];
    uint16_t ii = ioutHistory_mA[idx];

    int16_t yv = mapToScopeY(vv, vMin, vMax, plotTop, plotH);
    int16_t yi = mapToScopeY(ii, iMin, iMax, plotTop, plotH);

    if (havePrev) {
      tft.drawLine(x - 1, prevYv, x, yv, ST77XX_CYAN);
      tft.drawLine(x - 1, prevYi, x, yi, ST77XX_YELLOW);
    }
    prevYv = yv;
    prevYi = yi;
    havePrev = true;
  }
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
  const uint8_t textSize = UI_TEXT_SIZE;
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
    tft.fillRect(0, y, DISPLAY_WIDTH, lineHeight, bg);
    tft.setCursor(0, y);
    tft.print(text);
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
  tft.setSPISpeed(TFT_SPI_HZ);
  delay(50);
  // Important: use MODE3 for the module
  tft.init(240, 240, TFT_SPI_MODE);
  tft.setRotation(2);
  delay(50);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextWrap(true);
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.println("Initialize...");
  backlight(true);
  Serial.println("init power module");
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(10000);   // Drop speed to 10kHz

  // Workaround for SW3518S VIN reading (h1_SW35xx issue #9):
  // enable VIN ADC reading only after I2C is initialized and stable.
  delay(20);                                                 
  sw.begin();
  delay(20);

  sw.setMaxCurrent5A();
  Serial.println("SW3518 Connected Successfully!");
}

void loop() {
  sw.readStatus();

  drawStatusToDisplay();

  Serial.println("=======================================");
  Serial.printf("Current input voltage:%dmV\n", sw.vin_mV);
  Serial.printf("Current output voltage:%dmV\n", sw.vout_mV);
  Serial.printf("Current USB-C current:%dmA\r\n", sw.iout_usbc_mA);
  Serial.printf("Current USB-A current:%dmA\r\n", sw.iout_usba_mA);
  Serial.printf("Current fast charge type:%s\n", fastChargeType2String(sw.fastChargeType));
  if (sw.fastChargeType == SW35xx::PD_FIX || sw.fastChargeType == SW35xx::PD_PPS)
    Serial.printf("Current PD version:%d\n", sw.PDVersion);
  Serial.println("=======================================");
  Serial.println("");
  delay(2000);
}