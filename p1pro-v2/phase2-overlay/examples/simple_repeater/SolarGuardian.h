#pragma once

#include <Arduino.h>
#include <target.h>
#include "MyMesh.h"

class SolarGuardian {
public:
  enum PowerState : uint8_t {
    NORMAL = 0,
    ECO = 1,
    SURVIVAL = 2,
    CRITICAL = 3
  };

private:
  SenseCapSolarBoard& board;
  MyMesh& mesh;
  uint32_t next_sample = 0;
  uint8_t state = NORMAL;
  uint8_t worse_samples = 0;
  uint8_t better_samples = 0;
  uint8_t shutdown_samples = 0;
  uint16_t last_mv = 0;

  uint8_t classify(uint16_t mv, bool external) const;
  void applyPolicy();

public:
  SolarGuardian(SenseCapSolarBoard& b, MyMesh& m) : board(b), mesh(m) {}
  void begin();
  void loop();

  uint8_t getState() const { return state; }
  uint16_t getLastMv() const { return last_mv; }
};
