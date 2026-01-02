#include "sw3518.h"

#include <Wire.h>

using namespace h1_SW35xx;

static constexpr int I2C_SDA = 23;
static constexpr int I2C_SCL = 20;
static constexpr uint32_t I2C_CLOCK_HZ = 10000;

static SW35xx sw(Wire);
static Sw3518Status gStatus = {0, 0, 0, 0, SW35xx::NOT_FAST_CHARGE, 0, 0};

void sw3518Init() {
  Serial.println("init power module");
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(I2C_CLOCK_HZ);
#if defined(ARDUINO_ARCH_ESP32)
  Wire.setTimeout(50);
#endif

  // Workaround for SW3518S VIN reading (h1_SW35xx issue #9): enable VIN ADC after I2C is stable.
  delay(20);
  sw.begin();
  delay(20);

  sw.setMaxCurrent5A();
  Serial.println("SW3518 Connected Successfully!");
}

void sw3518ReadStatus() {
  sw.readStatus();
  gStatus.vin_mV = sw.vin_mV;
  gStatus.vout_mV = sw.vout_mV;
  gStatus.iout_usbc_mA = sw.iout_usbc_mA;
  gStatus.iout_usba_mA = sw.iout_usba_mA;
  gStatus.fastChargeType = sw.fastChargeType;
  gStatus.pdVersion = sw.PDVersion;
  gStatus.lastUpdateMs = millis();
}

const Sw3518Status &sw3518GetStatus() { return gStatus; }

const char *fastChargeType2String(SW35xx::fastChargeType_t fastChargeType) {
  switch (fastChargeType) {
  case SW35xx::NOT_FAST_CHARGE:
    return "Slow charge";
  case SW35xx::QC2:
    return "QC2.0";
  case SW35xx::QC3:
    return "QC3.0";
  case SW35xx::FCP:
    return "FCP";
  case SW35xx::SCP:
    return "SCP";
  case SW35xx::PD_FIX:
    return "PD Fix";
  case SW35xx::PD_PPS:
    return "PD PPS";
  case SW35xx::MTKPE1:
    return "MTK PE1.1";
  case SW35xx::MTKPE2:
    return "MTK PE2.0";
  case SW35xx::LVDC:
    return "LVDC";
  case SW35xx::SFCP:
    return "SFCP";
  case SW35xx::AFC:
    return "AFC";
  default:
    return "unknown";
  }
}
