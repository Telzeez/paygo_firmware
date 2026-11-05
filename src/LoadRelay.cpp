#include "LoadRelay.h"

#include "HardwareConfig.h"

void LoadRelay::begin() {
  pinMode(HardwareConfig::RELAY_PIN, OUTPUT);
  setOn(false);
}

void LoadRelay::setOn(bool on) {
  isOn_ = on;
  digitalWrite(HardwareConfig::RELAY_PIN,
               on ? HardwareConfig::RELAY_ON_LEVEL : HardwareConfig::RELAY_OFF_LEVEL);
}

bool LoadRelay::isOn() const {
  return isOn_;
}
