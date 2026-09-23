#include "BaseStationSupervisor.h"

#define P1_SUPERVISOR_INTERVAL_MS 5000UL
#define P1_POOL_STALL_REBOOT_MS 120000UL

void BaseStationSupervisor::begin() {
  uint32_t now = millis();
  next_check = now + P1_SUPERVISOR_INTERVAL_MS;
  last_progress_at = now;
  last_traffic_total =
      mesh.getNumSentFlood() + mesh.getNumSentDirect() +
      mesh.getNumRecvFlood() + mesh.getNumRecvDirect();
}

void BaseStationSupervisor::loop() {
  const uint32_t now = millis();
  if ((int32_t)(now - next_check) < 0) return;
  next_check = now + P1_SUPERVISOR_INTERVAL_MS;

  const int free_packets = mesh.baseStationFreePackets();
  const int tx_queued = mesh.baseStationTxQueued();
  const int rx_queued = mesh.baseStationRxQueued();

  const uint32_t traffic_total =
      mesh.getNumSentFlood() + mesh.getNumSentDirect() +
      mesh.getNumRecvFlood() + mesh.getNumRecvDirect();

  if (traffic_total != last_traffic_total) {
    last_traffic_total = traffic_total;
    last_progress_at = now;
  }

  if (mesh.baseStationCongested()) {
    congestion_events++;
  }

  // Exhaustion is only treated as a hard stall when the bounded pool has
  // remained completely empty and no packet progress has occurred for two
  // minutes. Normal bursts therefore never cause a reboot.
  if (free_packets == 0) {
    if (zero_free_since == 0) zero_free_since = now;

    const bool pool_stalled =
        (uint32_t)(now - zero_free_since) >= P1_POOL_STALL_REBOOT_MS;
    const bool no_progress =
        (uint32_t)(now - last_progress_at) >= P1_POOL_STALL_REBOOT_MS;

    if (pool_stalled && no_progress) {
      pool_stall_recoveries++;
      MESH_DEBUG_PRINTLN(
          "P1 supervisor: packet pool stalled (tx=%d rx=%d), rebooting",
          tx_queued, rx_queued);
      board.reboot();
    }
  } else {
    zero_free_since = 0;
  }
}
