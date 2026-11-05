#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include "BalanceStore.h"
#include "DeviceState.h"
#include "FrontPanel.h"
#include "LoadRelay.h"
#include "LowBalanceBuzzer.h"
#include "PayGoApi.h"
#include "PzemMeter.h"

class PayGoApp {
 public:
  void setup();
  void loop();

 private:
  static void fetchTaskEntry(void* parameter);
  void fetchTask();
  bool syncInitialBalance();
  void sendHeartbeat();
  void pushBalance();
  void updateEnergyLogic();
  void applyOutputState();
  void saveToStorage();
  EnergySnapshot snapshot();
  double availableEnergyKwh();
  void logMeterFailure(PzemStatus status, uint8_t bytesReceived);

  PzemMeter meter_;
  BalanceStore store_;
  PayGoApi api_;
  FrontPanel panel_;
  LoadRelay relay_;
  LowBalanceBuzzer buzzer_;

  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  double balanceKwh_ = 0;
  double pendingKwh_ = 0;
  double inflightKwh_ = 0;
  double availableKwh_ = 0;
  bool relayDesiredOn_ = false;
  volatile bool needSave_ = false;

  PzemData meterData_;
  double lastPzemKwh_ = 0;
  bool pzemSeeded_ = false;
  uint32_t lastStorageAt_ = 0;
  uint32_t lastMeterReadAt_ = 0;
};
