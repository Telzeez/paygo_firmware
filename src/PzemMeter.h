#pragma once

#include <Arduino.h>
#include "DeviceState.h"

class PzemMeter {
 public:
  void begin();
  PzemStatus read(PzemData& output, uint8_t& bytesReceived);

 private:
  static uint16_t crc16(const uint8_t* data, uint8_t length);
  PzemStatus readOnce(PzemData& output, uint8_t& bytesReceived);
};
