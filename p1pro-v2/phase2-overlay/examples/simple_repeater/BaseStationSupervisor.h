#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include "MyMesh.h"

class BaseStationSupervisor {
  mesh::MainBoard& board;
  MyMesh& mesh;
  uint32_t next_check = 0;
  uint32_t zero_free_since = 0;
  uint32_t last_progress_at = 0;
  uint32_t last_traffic_total = 0;
  uint32_t congestion_events = 0;
  uint32_t queue_recoveries = 0;
  uint32_t radio_recoveries = 0;
  uint32_t full_reboots = 0;
  uint8_t recovery_stage = 0;

public:
  BaseStationSupervisor(mesh::MainBoard& b, MyMesh& m) : board(b), mesh(m) {}
  void begin();
  void loop();

  uint32_t getCongestionEvents() const { return congestion_events; }
  uint32_t getQueueRecoveries() const { return queue_recoveries; }
  uint32_t getRadioRecoveries() const { return radio_recoveries; }
  uint32_t getFullReboots() const { return full_reboots; }
};
