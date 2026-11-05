#pragma once

#include <Arduino.h>

struct PzemData {
  float voltage = 0;
  float current = 0;
  float power = 0;
  float energy = 0;
  float frequency = 0;
  float powerFactor = 0;
};

enum class PzemStatus {
  OK,
  NO_REPLY,
  BAD_FRAME,
  BAD_CRC
};

struct EnergySnapshot {
  double balanceKwh = 0;
  double pendingKwh = 0;
  double inflightKwh = 0;
  double lastPzemKwh = 0;
  bool pzemSeeded = false;
};

struct BalanceReply {
  int httpStatus = 0;
  bool hasBalance = false;
  double balanceKwh = 0;
  String error;
};
