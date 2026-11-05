#pragma once

#include <Arduino.h>

namespace HardwareConfig {
constexpr uint8_t PZEM_RX_PIN = 16;
constexpr uint8_t PZEM_TX_PIN = 17;
constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;
constexpr uint8_t RELAY_PIN = 5;
constexpr uint8_t BUZZER_PIN = 23;
constexpr uint8_t CONFIRM_BUTTON_PIN = 4;

constexpr uint8_t RELAY_ON_LEVEL = HIGH;
constexpr uint8_t RELAY_OFF_LEVEL = LOW;

constexpr uint8_t LCD_ADDRESS = 0x27;
constexpr uint8_t PZEM_ADDRESS = 0xF8;
constexpr uint32_t PZEM_BAUD_RATE = 9600;
constexpr uint8_t PZEM_READ_ATTEMPTS = 3;
constexpr float VOLTAGE_CALIBRATION = 1.0f;
}  // namespace HardwareConfig

namespace AppConfig {
constexpr uint32_t FETCH_INTERVAL_MS = 5000;
constexpr uint32_t METER_READ_INTERVAL_MS = 1000;
constexpr uint32_t STORAGE_INTERVAL_MS = 30000;
constexpr uint32_t DISPLAY_PAGE_INTERVAL_MS = 3000;
constexpr uint32_t BUZZER_INTERVAL_MS = 5000;
constexpr uint32_t BUZZER_DURATION_MS = 200;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 10000;
constexpr uint32_t HTTP_TIMEOUT_MS = 10000;

constexpr double MIN_PUSH_KWH = 0.001;
constexpr double BALANCE_EQUALITY_EPSILON_KWH = 0.0005;
constexpr double RELAY_MINIMUM_KWH = 0.001;
constexpr double LOW_BALANCE_WARNING_KWH = 1.0;
constexpr uint8_t DISPLAY_PAGE_COUNT = 2;
}  // namespace AppConfig
