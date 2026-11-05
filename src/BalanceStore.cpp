#include "BalanceStore.h"

bool BalanceStore::begin() {
  ready_ = preferences_.begin("paygo", false);
  return ready_;
}

bool BalanceStore::load(EnergySnapshot& snapshot) {
  if (!ready_) {
    return false;
  }

  snapshot.balanceKwh = preferences_.getDouble("bal2", 0.0);
  snapshot.pendingKwh = preferences_.getDouble("pend2", 0.0);
  snapshot.inflightKwh = preferences_.getDouble("infl2", 0.0);
  snapshot.pzemSeeded = preferences_.isKey("pz");
  if (snapshot.pzemSeeded) {
    snapshot.lastPzemKwh = preferences_.getDouble("pz", 0.0);
  }
  return true;
}

bool BalanceStore::save(const EnergySnapshot& snapshot) {
  if (!ready_) {
    return false;
  }

  const size_t balanceBytes = preferences_.putDouble("bal2", snapshot.balanceKwh);
  const size_t pendingBytes = preferences_.putDouble("pend2", snapshot.pendingKwh);
  const size_t inflightBytes = preferences_.putDouble("infl2", snapshot.inflightKwh);
  const size_t pzemBytes = preferences_.putDouble("pz", snapshot.lastPzemKwh);
  return balanceBytes > 0 && pendingBytes > 0 && inflightBytes > 0 && pzemBytes > 0;
}
