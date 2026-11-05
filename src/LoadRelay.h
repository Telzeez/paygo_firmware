#pragma once

#include <Arduino.h>

class LoadRelay {
 public:
  void begin();
  void setOn(bool on);
  bool isOn() const;

 private:
  bool isOn_ = false;
};
