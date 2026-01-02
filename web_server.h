#pragma once

#include "sw3518.h"

bool webEnabled();
void webInit();
void webLoop(uint32_t nowMs);
void webUpdateStatus(const Sw3518Status &status, uint32_t nowMs);
