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
    Serial.println("Current:===============================");
    Serial.printf("Input voltage:%dmV\n", status.vin_mV);
    Serial.printf("Output voltage:%dmV\n", status.vout_mV);
    Serial.printf("USB-C current:%dmA\r\n", status.iout_usbc_mA);
    Serial.printf("USB-A current:%dmA\r\n", status.iout_usba_mA);
    Serial.printf("Fast charge type:%s\n", fastChargeType2String(status.fastChargeType));
    if (status.fastChargeType == SW35xx::PD_FIX || status.fastChargeType == SW35xx::PD_PPS) {
      Serial.printf("PD version:%d\n", status.pdVersion);
    }
    Serial.println("=======================================");
    Serial.println("");
  }

  delay(1);
}
