#include "AdaptiveMeshController.h"

#define V2_ADAPTIVE_INTERVAL_MS 5000UL
#define V2_RECOVERY_QUIET_SAMPLES 6

uint8_t AdaptiveMeshController::classify() const {
  const int free_packets = mesh.baseStationFreePackets();
  const int tx = mesh.baseStationTxQueued();
  const int rx = mesh.baseStationRxQueued();
  const int qmax = tx > rx ? tx : rx;
  const uint32_t drops = mesh.baseStationDroppedTx() + mesh.baseStationDroppedRx();
  const bool new_drop = drops != last_drop_total;

  if (free_packets <= 6 || qmax >= 24) return SEVERE;
  if (new_drop || free_packets <= 10 || qmax >= 16) return CONGESTED;
  if (free_packets <= 16 || qmax >= 8) return BUSY;
  return NORMAL;
}

void AdaptiveMeshController::applyPolicy() {
  // airtime_factor: effective duty cycle = 100 / (factor + 1)
  // NORMAL=10%, BUSY=5%, CONGESTED=2%, SEVERE=1%.
  static const float factors[] = {9.0f, 19.0f, 49.0f, 99.0f};
  const bool cad = state != NORMAL;
  mesh.setV2AdaptiveMeshPolicy(factors[state], cad, state);
}

void AdaptiveMeshController::begin() {
  next_check = millis() + V2_ADAPTIVE_INTERVAL_MS;
  last_drop_total = mesh.baseStationDroppedTx() + mesh.baseStationDroppedRx();
  state = NORMAL;
  quiet_samples = 0;
  applyPolicy();
}

void AdaptiveMeshController::loop() {
  const uint32_t now = millis();
  if ((int32_t)(now - next_check) < 0) return;
  next_check = now + V2_ADAPTIVE_INTERVAL_MS;

  const uint8_t target = classify();

  // Escalate immediately; recover one level only after 30 seconds quiet.
  if (target > state) {
    state = target;
    quiet_samples = 0;
  } else if (target < state) {
    if (++quiet_samples >= V2_RECOVERY_QUIET_SAMPLES) {
      state--;
      quiet_samples = 0;
    }
  } else {
    quiet_samples = 0;
  }

  last_drop_total = mesh.baseStationDroppedTx() + mesh.baseStationDroppedRx();
  applyPolicy();
}
