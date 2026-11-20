#include "FrontPanel.h"

#include <Wire.h>
#include <cstring>

#include "DeviceConfig.h"
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

void FrontPanel::render(const PzemData& meter, double availableKwh, bool meterValid,
                        bool backendOnline, uint8_t assignmentState) {
  char line1[32] = {0};
  char line2[32] = {0};

  if (page_ == 0) {
    if (meterValid) {
      snprintf(line1, sizeof(line1), "V:%.0f F:%.0fHz", meter.voltage, meter.frequency);
      if (meter.current < 10.0f) {
        snprintf(line2, sizeof(line2), "I:%.1fA P%.0fW", meter.current, meter.power);
      } else {
        snprintf(line2, sizeof(line2), "I:%.0fA P%.0fW", meter.current, meter.power);
      }
    } else {
      snprintf(line1, sizeof(line1), "V:--- F:--Hz");
      snprintf(line2, sizeof(line2), "I:--- P:---W");
    }
  } else if (page_ == 1) {
    if (meterValid) {
      snprintf(line1, sizeof(line1), "E:%.2fkWh", meter.energy);
    } else {
      snprintf(line1, sizeof(line1), "E:--");
    }
    snprintf(line2, sizeof(line2), "B:%.2fkWh", availableKwh);
  } else {
    formatDeviceLabel(line1, sizeof(line1));
    const char* assignmentLabel = "UNAS";
    switch (assignmentState) {
      case 1: assignmentLabel = "ASGN"; break;
      case 2: assignmentLabel = "ACTV"; break;
      case 3: assignmentLabel = "SUSP"; break;
      default: break;
    }
    snprintf(line2, sizeof(line2), "%s|%s",
             backendOnline ? "ONLINE" : "OFFLINE", assignmentLabel);
  }

  writeLine(0, line1);
  writeLine(1, line2);
}

void FrontPanel::formatDeviceLabel(char* output, size_t outputSize) const {
  const char* deviceId = PAYGO_DEVICE_ID;
  size_t end = strlen(deviceId);
  size_t start = end;

  while (start > 0 && deviceId[start - 1] >= '0' && deviceId[start - 1] <= '9') {
    --start;
  }

  char suffix[5] = "0001";
  const size_t digitCount = end - start;
  if (digitCount > 0) {
    const size_t copyCount = digitCount > 4 ? 4 : digitCount;
    memset(suffix, '0', 4);
    memcpy(suffix + (4 - copyCount), deviceId + end - copyCount, copyCount);
    suffix[4] = '\0';
  } else {
    snprintf(suffix, sizeof(suffix), "----");
  }

  snprintf(output, outputSize, "PAYGO #%s", suffix);
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
  if (row >= HardwareConfig::LCD_ROWS) {
    return;
  }

  char line[HardwareConfig::LCD_COLUMNS + 1];
  snprintf(line, sizeof(line), "%-*.*s",
           static_cast<int>(HardwareConfig::LCD_COLUMNS),
           static_cast<int>(HardwareConfig::LCD_COLUMNS),
           text ? text : "");

  if (lineDrawn_[row] && strcmp(lastLines_[row], line) == 0) {
    return;
  }

  lcd_.setCursor(0, row);
  lcd_.print(line);
  memcpy(lastLines_[row], line, sizeof(line));
  lineDrawn_[row] = true;
}
