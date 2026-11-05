#include "LowBalanceBuzzer.h"

#include "HardwareConfig.h"

void LowBalanceBuzzer::begin() {
  pinMode(HardwareConfig::BUZZER_PIN, OUTPUT);
  digitalWrite(HardwareConfig::BUZZER_PIN, LOW);
}

void LowBalanceBuzzer::update(double availableKwh, uint32_t now) {
  const bool lowBalance = availableKwh > 0 &&
                          availableKwh <= AppConfig::LOW_BALANCE_WARNING_KWH;

  if (lowBalance && !active_ && now - lastBeepAt_ >= AppConfig::BUZZER_INTERVAL_MS) {
    lastBeepAt_ = now;
    active_ = true;
    digitalWrite(HardwareConfig::BUZZER_PIN, HIGH);
  }
  if (active_ && now - lastBeepAt_ >= AppConfig::BUZZER_DURATION_MS) {
    active_ = false;
    digitalWrite(HardwareConfig::BUZZER_PIN, LOW);
  }
  if (!lowBalance && active_) {
    active_ = false;
    digitalWrite(HardwareConfig::BUZZER_PIN, LOW);
  }
}
