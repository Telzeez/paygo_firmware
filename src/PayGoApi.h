#pragma once

#include <Arduino.h>
#include "DeviceState.h"

class PayGoApi {
 public:
  void beginWifi();
  bool isConnected() const;
  void reconnectWifi();
  bool waitForSystemTime(uint32_t timeoutMs);
  BalanceReply postHeartbeat();
  BalanceReply putBalance(double balanceKwh, double expectedKwh);

 private:
  BalanceReply sendRequest(const char* method, const String& url, const String& body);
  static bool parseBalance(const String& body, double& balanceKwh);
  String deviceEndpoint(const char* suffix) const;
};
