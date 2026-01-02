#include <Arduino.h>

#include "display_ui.h"
#include "sw3518.h"
#include "web_server.h"

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.setDebugOutput(true);
  Serial.println("\nCustom charger init");
  Serial.flush();

  displayInit();
  sw3518Init();

  if (webEnabled()) {
    webInit();
  }

  delay(2000);
}

void loop() {
  static constexpr uint32_t READ_STATUS_MS = 200;
  static constexpr uint32_t UI_REFRESH_MS = 200;
  static constexpr uint32_t SERIAL_LOG_MS = 2000;

  static uint32_t lastReadMs = (uint32_t)(0 - READ_STATUS_MS);
  static uint32_t lastUiMs = (uint32_t)(0 - UI_REFRESH_MS);
  static uint32_t lastSerialMs = (uint32_t)(0 - SERIAL_LOG_MS);

  const uint32_t now = millis();

  webLoop(now);

  if ((uint32_t)(now - lastReadMs) >= READ_STATUS_MS) {
    lastReadMs = now;
    yield();
    sw3518ReadStatus();
    if (webEnabled()) {
      webUpdateStatus(sw3518GetStatus(), now);
    }
    yield();
  }

  const Sw3518Status &status = sw3518GetStatus();

  if ((uint32_t)(now - lastUiMs) >= UI_REFRESH_MS) {
    lastUiMs = now;
    displayRender(status);
  }

  if ((uint32_t)(now - lastSerialMs) >= SERIAL_LOG_MS) {
    lastSerialMs = now;
    Serial.print('\r');
    Serial.printf("VIN:%dmV VOUT:%dmV USBC:%dmA USBA:%dmA FC:%s",
            status.vin_mV, status.vout_mV, status.iout_usbc_mA, status.iout_usba_mA,
            fastChargeType2String(status.fastChargeType));
    if (status.fastChargeType == SW35xx::PD_FIX || status.fastChargeType == SW35xx::PD_PPS) {
      Serial.printf(" PD:%d", status.pdVersion);
    }
    Serial.print("          "); // pad to overwrite remnants from longer previous line
    Serial.flush();
  }

  delay(1);
}
