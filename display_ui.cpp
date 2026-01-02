#include "display_ui.h"

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

#ifndef ST77XX_DARKGREY
#define ST77XX_DARKGREY 0x7BEF
#endif

static constexpr int DISPLAY_WIDTH = 240;
static constexpr int DISPLAY_HEIGHT = 240;

static constexpr int TFT_DC = 11;
static constexpr int TFT_RST = 10;
static constexpr int TFT_MOSI = 7;
static constexpr int TFT_MISO = 2;
static constexpr int TFT_SCLK = 6;
static constexpr int TFT_CS = -1;
static constexpr int TFT_BLK = 3;
static constexpr int TFT_SPI_MODE = SPI_MODE3;
static constexpr uint32_t TFT_SPI_HZ = 80000000;

static constexpr int TFT_BLK_ON = 1;
static constexpr int TFT_BLK_OFF = !TFT_BLK_ON;

static Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);

static void backlight(bool on) {
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, on ? TFT_BLK_ON : TFT_BLK_OFF);
}

// --- Display layout ---
static constexpr uint8_t UI_TEXT_SIZE = 2;
static constexpr int16_t UI_LINE_HEIGHT = 8 * UI_TEXT_SIZE + 2;
static constexpr uint8_t UI_TEXT_LINES = 8; // lines 0..7 used today
static constexpr int16_t SCOPE_Y = UI_TEXT_LINES * UI_LINE_HEIGHT;
static constexpr int16_t SCOPE_H = DISPLAY_HEIGHT - SCOPE_Y;
static constexpr int16_t SCOPE_W = DISPLAY_WIDTH;

// Optional off-screen rendering to reduce flicker.
static GFXcanvas16 *scopeCanvas = nullptr;
static GFXcanvas16 *textLineCanvas = nullptr;

// Oscilloscope buffers (one sample per loop).
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
  int32_t scaled = (int32_t)(clamped - minValue) * (height - 1) / range;
  return (int16_t)(yTop + (height - 1) - scaled);
}

static void drawOscilloscope(uint16_t vout_mV, uint16_t iout_total_mA) {
  if (SCOPE_H <= 12) return;

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

  if ((uint16_t)(vMax - vMin) < 50) {
    const uint16_t mid = (uint16_t)(((uint32_t)vMax + (uint32_t)vMin) / 2u);
    vMin = (mid > 25) ? (uint16_t)(mid - 25) : 0;
    vMax = (uint16_t)(mid + 25);
  }
  if ((uint16_t)(iMax - iMin) < 50) {
    const uint16_t mid = (uint16_t)(((uint32_t)iMax + (uint32_t)iMin) / 2u);
    iMin = (mid > 25) ? (uint16_t)(mid - 25) : 0;
    iMax = (uint16_t)(mid + 25);
  }

  static bool scaleInit = false;
  static uint16_t vMinS, vMaxS, iMinS, iMaxS;
  if (!scaleInit) {
    vMinS = vMin;
    vMaxS = vMax;
    iMinS = iMin;
    iMaxS = iMax;
    scaleInit = true;
  } else {
    vMinS = (uint16_t)(((uint32_t)vMinS * 7u + (uint32_t)vMin) / 8u);
    vMaxS = (uint16_t)(((uint32_t)vMaxS * 7u + (uint32_t)vMax) / 8u);
    iMinS = (uint16_t)(((uint32_t)iMinS * 7u + (uint32_t)iMin) / 8u);
    iMaxS = (uint16_t)(((uint32_t)iMaxS * 7u + (uint32_t)iMax) / 8u);
  }
  if (vMaxS <= vMinS) vMaxS = (uint16_t)(vMinS + 1);
  if (iMaxS <= iMinS) iMaxS = (uint16_t)(iMinS + 1);

  const int16_t innerTop = 1;
  const int16_t innerBottom = SCOPE_H - 2;
  const int16_t innerH = innerBottom - innerTop + 1;
  const int16_t dividerY = innerTop + innerH / 2;

  const int16_t vTop = innerTop;
  const int16_t vH = dividerY - innerTop;
  const int16_t iTop = dividerY + 1;
  const int16_t iH = innerBottom - iTop + 1;
  if (vH <= 1 || iH <= 1) return;

  auto renderScope = [&](auto &gfx, int16_t originY) {
    gfx.fillRect(0, originY, SCOPE_W, SCOPE_H, ST77XX_BLACK);
    gfx.drawRect(0, originY, SCOPE_W, SCOPE_H, ST77XX_DARKGREY);
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
    renderScope(*scopeCanvas, 0);
    tft.drawRGBBitmap(0, SCOPE_Y, scopeCanvas->getBuffer(), SCOPE_W, SCOPE_H);
    return;
  }

  renderScope(tft, SCOPE_Y);
}

void displayInit() {
  Serial.println("init display");
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);
  delay(50);
  tft.init(DISPLAY_WIDTH, DISPLAY_HEIGHT, TFT_SPI_MODE);
  tft.setSPISpeed(TFT_SPI_HZ);
  tft.setRotation(2);
  delay(50);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(UI_TEXT_SIZE);
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
}

void displayRender(const Sw3518Status &status) {
  const uint16_t fg = ST77XX_WHITE;
  const uint16_t bg = ST77XX_BLACK;
  const int16_t lineHeight = UI_LINE_HEIGHT;

  tft.setTextColor(fg, bg);
  tft.setTextWrap(false);

  auto drawLine = [&](uint8_t line, const char *text) {
    static char last[10][40];
    if (line >= 10) return;
    if (strncmp(last[line], text, sizeof(last[line])) == 0) return;
    strncpy(last[line], text, sizeof(last[line]) - 1);
    last[line][sizeof(last[line]) - 1] = '\0';

    int16_t y = line * lineHeight;

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

  snprintf(buf, sizeof(buf), "VIN : %d mV", status.vin_mV);
  drawLine(2, buf);

  snprintf(buf, sizeof(buf), "VOUT: %d mV", status.vout_mV);
  drawLine(3, buf);

  snprintf(buf, sizeof(buf), "USB-C: %d mA", status.iout_usbc_mA);
  drawLine(4, buf);

  snprintf(buf, sizeof(buf), "USB-A: %d mA", status.iout_usba_mA);
  drawLine(5, buf);

  snprintf(buf, sizeof(buf), "Type: %s", fastChargeType2String(status.fastChargeType));
  drawLine(6, buf);

  if (status.fastChargeType == SW35xx::PD_FIX || status.fastChargeType == SW35xx::PD_PPS) {
    snprintf(buf, sizeof(buf), "PD v: %d", status.pdVersion);
    drawLine(7, buf);
  } else {
    drawLine(7, "");
  }

  uint16_t total_mA = (uint16_t)(status.iout_usbc_mA + status.iout_usba_mA);
  drawOscilloscope((uint16_t)status.vout_mV, total_mA);
}
