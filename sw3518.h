#pragma once

#include <Arduino.h>
#include <h1_SW35xx.h>

using namespace h1_SW35xx;

struct Sw3518Status {
  int vin_mV;
  int vout_mV;
  int iout_usbc_mA;
  int iout_usba_mA;
  SW35xx::fastChargeType_t fastChargeType;
  int pdVersion;
  uint32_t lastUpdateMs;
};

void sw3518Init();
void sw3518ReadStatus();
const Sw3518Status &sw3518GetStatus();
const char *fastChargeType2String(SW35xx::fastChargeType_t fastChargeType);
