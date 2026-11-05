#pragma once

#include <Arduino.h>

class LowBalanceBuzzer {
 public:
  void begin();
  void update(double availableKwh, uint32_t now);

 private:
  uint32_t lastBeepAt_ = 0;
  bool active_ = false;
};
