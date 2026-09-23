#include "BaseStationSupervisor.h"

#define P1_SUPERVISOR_INTERVAL_MS 5000UL
#define V2_RECOVERY_STAGE_MS 60000UL

void BaseStationSupervisor::begin() {
  const uint32_t now = millis();
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
  const uint32_t traffic_total =
      mesh.getNumSentFlood() + mesh.getNumSentDirect() +
      mesh.getNumRecvFlood() + mesh.getNumRecvDirect();

  if (traffic_total != last_traffic_total) {
    last_traffic_total = traffic_total;
    last_progress_at = now;
    zero_free_since = 0;
    recovery_stage = 0;
  }

  if (mesh.baseStationCongested()) congestion_events++;

  if (free_packets > 0) {
    zero_free_since = 0;
    recovery_stage = 0;
    return;
  }

  if (zero_free_since == 0) zero_free_since = now;

  const bool no_progress =
      (uint32_t)(now - last_progress_at) >= V2_RECOVERY_STAGE_MS;
  const bool stage_elapsed =
      (uint32_t)(now - zero_free_since) >= V2_RECOVERY_STAGE_MS;

  if (!no_progress || !stage_elapsed) return;

  if (recovery_stage == 0) {
    const int recovered = mesh.v2RecoverPacketQueues();
    queue_recoveries++;
    recovery_stage = 1;
    zero_free_since = now;
    MESH_DEBUG_PRINTLN("P1 V2 supervisor: queue recovery freed %d packets", recovered);
    return;
  }

  if (recovery_stage == 1) {
    const bool ok = mesh.v2RecoverRadio();
    radio_recoveries++;
    recovery_stage = 2;
    zero_free_since = now;
    MESH_DEBUG_PRINTLN("P1 V2 supervisor: radio recovery %s", ok ? "OK" : "FAILED");
    return;
  }

  full_reboots++;
  MESH_DEBUG_PRINTLN("P1 V2 supervisor: staged recovery exhausted, rebooting");
  board.reboot();
}
