#pragma once

#include <Preferences.h>
#include "DeviceState.h"

class BalanceStore {
 public:
  bool begin();
  bool load(EnergySnapshot& snapshot);
  bool save(const EnergySnapshot& snapshot);

 private:
  Preferences preferences_;
  bool ready_ = false;
};
