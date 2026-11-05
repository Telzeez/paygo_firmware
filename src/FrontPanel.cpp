#include "FrontPanel.h"

#include <Wire.h>

#include "HardwareConfig.h"

void FrontPanel::begin() {
  Wire.begin(HardwareConfig::I2C_SDA_PIN, HardwareConfig::I2C_SCL_PIN);
  lcd_.init();
  lcd_.backlight();
  pinMode(HardwareConfig::CONFIRM_BUTTON_PIN, INPUT_PULLUP);
}

void FrontPanel::showMessage(const char* line1, const char* line2) {
  writeLine(0, line1);
  writeLine(1, line2);
}

void FrontPanel::render(const PzemData& meter, double availableKwh, bool relayOn) {
  char line1[24];
  char line2[24];
  if (page_ == 0) {
    snprintf(line1, sizeof(line1), "V:%.1f F:%.1fHz", meter.voltage, meter.frequency);
    snprintf(line2, sizeof(line2), "I:%.2fA P:%.0fW", meter.current, meter.power);
  } else {
    snprintf(line1, sizeof(line1), "Available:");
    snprintf(line2, sizeof(line2), "%.3f kWh %s", static_cast<float>(availableKwh),
             relayOn ? "ON" : "OFF");
  }
  writeLine(0, line1);
  writeLine(1, line2);
}

bool FrontPanel::updatePage(uint32_t now) {
  bool changed = false;
  const bool buttonState = digitalRead(HardwareConfig::CONFIRM_BUTTON_PIN);
  if (buttonState == LOW && lastButtonState_ == HIGH && now - lastButtonAt_ > 200) {
    lastButtonAt_ = now;
    page_ = (page_ + 1) % AppConfig::DISPLAY_PAGE_COUNT;
    lastPageAt_ = now;
    changed = true;
  }
  lastButtonState_ = buttonState;

  if (now - lastPageAt_ >= AppConfig::DISPLAY_PAGE_INTERVAL_MS) {
    lastPageAt_ = now;
    page_ = (page_ + 1) % AppConfig::DISPLAY_PAGE_COUNT;
    changed = true;
  }
  return changed;
}

void FrontPanel::writeLine(uint8_t row, const char* text) {
  char line[17];
  snprintf(line, sizeof(line), "%-16s", text);
  lcd_.setCursor(0, row);
  lcd_.print(line);
}
