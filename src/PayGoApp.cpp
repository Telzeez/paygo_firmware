#include "PayGoApp.h"

#include <WiFi.h>
#include <freertos/task.h>
#include <cmath>

#include "HardwareConfig.h"

void PayGoApp::setup() {
  Serial.begin(115200);
  relay_.begin();
  buzzer_.begin();
  panel_.begin();
  panel_.showMessage("PAYGO SOLAR", "Connecting WiFi.");
  meter_.begin();

  if (!store_.begin()) {
    Serial.println("Could not initialize device storage.");
  }

  api_.beginWifi();
  const uint32_t wifiStartedAt = millis();
  uint8_t dots = 0;
  while (!api_.isConnected() &&
         millis() - wifiStartedAt < AppConfig::WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    dots = (dots + 1) % 4;
    char line[17];
    snprintf(line, sizeof(line), "Connecting WiFi%.*s", dots, "...");
    panel_.showMessage("PAYGO SOLAR", line);
    Serial.print(".");
  }
  Serial.println();

  bool synced = false;
  if (api_.isConnected()) {
    panel_.showMessage("PAYGO SOLAR", "Syncing Server..");
    synced = syncInitialBalance();
  }

  if (synced) {
    panel_.showMessage("PAYGO SOLAR", "Sync Complete!");
    delay(1000);
  } else {
    EnergySnapshot saved;
    if (store_.load(saved)) {
      portENTER_CRITICAL(&mux_);
      balanceKwh_ = saved.balanceKwh;
      pendingKwh_ = saved.pendingKwh;
      inflightKwh_ = saved.inflightKwh;
      portEXIT_CRITICAL(&mux_);
      lastPzemKwh_ = saved.lastPzemKwh;
      pzemSeeded_ = saved.pzemSeeded;
    }
    panel_.showMessage("PAYGO SOLAR", "Offline Mode");
    delay(1500);
  }

  uint8_t bytesReceived = 0;
  if (meter_.read(meterData_, bytesReceived) == PzemStatus::OK) {
    lastPzemKwh_ = meterData_.energy;
    pzemSeeded_ = true;
  }

  updateEnergyLogic();
  applyOutputState();
  panel_.render(meterData_, availableEnergyKwh(), relay_.isOn());
  lastStorageAt_ = millis();

  const BaseType_t taskCreated = xTaskCreatePinnedToCore(
      fetchTaskEntry, "fetch", 12288, this, 1, nullptr, 0);
  if (taskCreated != pdPASS) {
    Serial.println("Could not start the network synchronization task.");
  }
}

void PayGoApp::loop() {
  const uint32_t now = millis();
  bool renderPanel = false;

  if (now - lastMeterReadAt_ >= AppConfig::METER_READ_INTERVAL_MS) {
    lastMeterReadAt_ = now;
    uint8_t bytesReceived = 0;
    const PzemStatus status = meter_.read(meterData_, bytesReceived);

    if (status == PzemStatus::OK) {
      if (!pzemSeeded_) {
        lastPzemKwh_ = meterData_.energy;
        pzemSeeded_ = true;
        needSave_ = true;
      } else {
        double consumedKwh = meterData_.energy - lastPzemKwh_;
        if (consumedKwh < 0) {
          consumedKwh = meterData_.energy;
        }
        if (consumedKwh > 0) {
          portENTER_CRITICAL(&mux_);
          pendingKwh_ += consumedKwh;
          portEXIT_CRITICAL(&mux_);
          needSave_ = true;
        }
        lastPzemKwh_ = meterData_.energy;
      }

      updateEnergyLogic();
      applyOutputState();
      const EnergySnapshot current = snapshot();
      Serial.printf(
          "V: %.1f | I: %.3f A | P: %.1f W | PZEM: %.3f kWh | "
          "Srv: %.3f | Pend: %.4f | Infl: %.4f | Avail: %.3f | Relay: %s\n",
          meterData_.voltage, meterData_.current, meterData_.power, meterData_.energy,
          current.balanceKwh, current.pendingKwh, current.inflightKwh,
          static_cast<float>(availableEnergyKwh()), relay_.isOn() ? "ON" : "OFF");
    } else {
      logMeterFailure(status, bytesReceived);
      updateEnergyLogic();
      applyOutputState();
    }
    renderPanel = true;
  }

  if (needSave_ && now - lastStorageAt_ >= AppConfig::STORAGE_INTERVAL_MS) {
    saveToStorage();
  }

  applyOutputState();
  if (panel_.updatePage(now)) {
    renderPanel = true;
  }
  if (renderPanel) {
    panel_.render(meterData_, availableEnergyKwh(), relay_.isOn());
  }
  buzzer_.update(availableEnergyKwh(), now);
}

void PayGoApp::fetchTaskEntry(void* parameter) {
  static_cast<PayGoApp*>(parameter)->fetchTask();
}

void PayGoApp::fetchTask() {
  for (;;) {
    if (api_.isConnected()) {
      sendHeartbeat();
      pushBalance();
    } else {
      Serial.println("WiFi disconnected. Reconnecting...");
      api_.reconnectWifi();
    }
    vTaskDelay(pdMS_TO_TICKS(AppConfig::FETCH_INTERVAL_MS));
  }
}

bool PayGoApp::syncInitialBalance() {
  if (!api_.isConnected()) {
    return false;
  }
  if (!api_.waitForSystemTime(AppConfig::WIFI_CONNECT_TIMEOUT_MS)) {
    Serial.println("System time unavailable; HTTPS certificate validation cannot proceed.");
    return false;
  }

  const BalanceReply reply = api_.postHeartbeat();
  Serial.printf("Initial sync HTTP code: %d\n", reply.httpStatus);
  if (reply.httpStatus != 200 || !reply.hasBalance) {
    if (reply.error.length() > 0) {
      Serial.printf("Initial sync failed: %s\n", reply.error.c_str());
    }
    return false;
  }

  portENTER_CRITICAL(&mux_);
  balanceKwh_ = reply.balanceKwh;
  pendingKwh_ = 0;
  inflightKwh_ = 0;
  portEXIT_CRITICAL(&mux_);

  saveToStorage();
  Serial.printf("Initial sync SUCCESS: Server balance is %.3f kWh\n", reply.balanceKwh);
  return true;
}

void PayGoApp::sendHeartbeat() {
  const BalanceReply reply = api_.postHeartbeat();
  Serial.printf("Heartbeat HTTP code: %d\n", reply.httpStatus);

  if (reply.httpStatus == 200 && reply.hasBalance) {
    bool topupDetected = false;
    double topupKwh = 0;
    bool balanceChanged = false;

    portENTER_CRITICAL(&mux_);
    if (reply.balanceKwh > balanceKwh_ + AppConfig::BALANCE_EQUALITY_EPSILON_KWH) {
      topupKwh = reply.balanceKwh - balanceKwh_;
      balanceKwh_ = reply.balanceKwh;
      needSave_ = true;
      topupDetected = true;
    } else if (inflightKwh_ <= 0 && pendingKwh_ <= 0 &&
               fabs(balanceKwh_ - reply.balanceKwh) >
                   AppConfig::BALANCE_EQUALITY_EPSILON_KWH) {
      balanceKwh_ = reply.balanceKwh;
      needSave_ = true;
      balanceChanged = true;
    }
    portEXIT_CRITICAL(&mux_);

    if (topupDetected) {
      Serial.printf("Top-up detected: +%.3f kWh -> Server balance now %.3f kWh\n",
                    topupKwh, reply.balanceKwh);
    } else if (balanceChanged) {
      Serial.printf("Server balance synced: %.3f kWh\n", reply.balanceKwh);
    }
    updateEnergyLogic();
  } else if (reply.error.length() > 0) {
    Serial.printf("Heartbeat failed: %s (HTTP %d)\n",
                  reply.error.c_str(), reply.httpStatus);
  }
}

void PayGoApp::pushBalance() {
  portENTER_CRITICAL(&mux_);
  if (inflightKwh_ <= 0 && pendingKwh_ >= AppConfig::MIN_PUSH_KWH) {
    inflightKwh_ = pendingKwh_;
    pendingKwh_ = 0;
    needSave_ = true;
  }
  const double inflight = inflightKwh_;
  const double expected = balanceKwh_;
  portEXIT_CRITICAL(&mux_);

  if (inflight <= 0) {
    return;
  }

  double target = expected - inflight;
  if (target < 0) {
    target = 0;
  }

  const BalanceReply reply = api_.putBalance(target, expected);
  Serial.printf("Balance push HTTP %d (expected %.3f -> new %.3f)\n",
                reply.httpStatus, expected, target);

  if (reply.httpStatus == 200 || reply.httpStatus == 201 || reply.httpStatus == 409) {
    const double serverBalance = reply.hasBalance ? reply.balanceKwh : target;
    const bool applied = reply.httpStatus != 409 ||
                         (reply.hasBalance &&
                          fabs(serverBalance - target) <
                              AppConfig::BALANCE_EQUALITY_EPSILON_KWH);

    portENTER_CRITICAL(&mux_);
    if (reply.hasBalance || applied) {
      balanceKwh_ = serverBalance;
    }
    if (applied) {
      inflightKwh_ = 0;
    }
    needSave_ = true;
    portEXIT_CRITICAL(&mux_);

    Serial.printf("%s. Server balance now %.3f kWh\n",
                  applied ? "Push confirmed"
                          : "Balance changed on server (top-up), will re-send",
                  serverBalance);
    updateEnergyLogic();
  } else if (reply.error.length() > 0) {
    Serial.printf("Push failed: %s (HTTP %d, usage stays queued)\n",
                  reply.error.c_str(), reply.httpStatus);
  }
}

void PayGoApp::updateEnergyLogic() {
  portENTER_CRITICAL(&mux_);
  const double available = balanceKwh_ - pendingKwh_ - inflightKwh_;
  availableKwh_ = available < 0 ? 0 : available;
  relayDesiredOn_ = availableKwh_ > AppConfig::RELAY_MINIMUM_KWH;
  portEXIT_CRITICAL(&mux_);
}

void PayGoApp::applyOutputState() {
  bool relayDesiredOn;
  portENTER_CRITICAL(&mux_);
  relayDesiredOn = relayDesiredOn_;
  portEXIT_CRITICAL(&mux_);

  if (relay_.isOn() && !relayDesiredOn) {
    saveToStorage();
  }
  relay_.setOn(relayDesiredOn);
}

void PayGoApp::saveToStorage() {
  portENTER_CRITICAL(&mux_);
  EnergySnapshot saved;
  saved.balanceKwh = balanceKwh_;
  saved.pendingKwh = pendingKwh_;
  saved.inflightKwh = inflightKwh_;
  needSave_ = false;
  portEXIT_CRITICAL(&mux_);

  saved.lastPzemKwh = lastPzemKwh_;
  saved.pzemSeeded = pzemSeeded_;
  if (!store_.save(saved)) {
    Serial.println("Could not save meter state; it will be retried.");
    needSave_ = true;
    return;
  }
  lastStorageAt_ = millis();
}

EnergySnapshot PayGoApp::snapshot() {
  portENTER_CRITICAL(&mux_);
  EnergySnapshot current;
  current.balanceKwh = balanceKwh_;
  current.pendingKwh = pendingKwh_;
  current.inflightKwh = inflightKwh_;
  portEXIT_CRITICAL(&mux_);
  current.lastPzemKwh = lastPzemKwh_;
  current.pzemSeeded = pzemSeeded_;
  return current;
}

double PayGoApp::availableEnergyKwh() {
  portENTER_CRITICAL(&mux_);
  const double available = availableKwh_;
  portEXIT_CRITICAL(&mux_);
  return available;
}

void PayGoApp::logMeterFailure(PzemStatus status, uint8_t bytesReceived) {
  switch (status) {
    case PzemStatus::NO_REPLY:
      Serial.println("PZEM: No reply. Check power and RX/TX.");
      break;
    case PzemStatus::BAD_FRAME:
      Serial.printf("PZEM: Bad frame (%u bytes)\n", bytesReceived);
      break;
    case PzemStatus::BAD_CRC:
      Serial.println("PZEM: CRC mismatch.");
      break;
    case PzemStatus::OK:
      break;
  }
}
