#pragma once

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include "DeviceState.h"
#include "HardwareConfig.h"

class FrontPanel {
 public:
  void begin();
  void showMessage(const char* line1, const char* line2);
  void render(const PzemData& meter, double availableKwh, bool meterValid,
              bool backendOnline, uint8_t assignmentState);
  bool updatePage(uint32_t now);

 private:
  void writeLine(uint8_t row, const char* text);
  void formatDeviceLabel(char* output, size_t outputSize) const;

  LiquidCrystal_I2C lcd_{HardwareConfig::LCD_ADDRESS,
                         HardwareConfig::LCD_COLUMNS,
                         HardwareConfig::LCD_ROWS};
  uint8_t page_ = 0;
  uint32_t lastPageAt_ = 0;
  uint32_t lastButtonAt_ = 0;
  bool lastButtonState_ = HIGH;
  bool lineDrawn_[2] = {false, false};
  char lastLines_[2][HardwareConfig::LCD_COLUMNS + 1] = {{0}, {0}};
};
