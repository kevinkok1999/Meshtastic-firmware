#pragma once

#include <Arduino.h>
#include "MyMesh.h"

class AdaptiveMeshController {
public:
  enum PressureState : uint8_t {
    NORMAL = 0,
    BUSY = 1,
    CONGESTED = 2,
    SEVERE = 3
  };

private:
  MyMesh& mesh;
  uint32_t next_check = 0;
  uint32_t last_drop_total = 0;
  uint8_t state = NORMAL;
  uint8_t quiet_samples = 0;

  uint8_t classify() const;
  void applyPolicy();

public:
  explicit AdaptiveMeshController(MyMesh& m) : mesh(m) {}
  void begin();
  void loop();
};
