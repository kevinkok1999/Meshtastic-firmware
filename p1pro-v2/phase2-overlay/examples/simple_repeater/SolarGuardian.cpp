#include "SolarGuardian.h"

#define V2_POWER_SAMPLE_MS        30000UL
#define V2_POWER_ECO_MV           3700
#define V2_POWER_SURVIVAL_MV      3500
#define V2_POWER_CRITICAL_MV      3350
#define V2_POWER_BOOTLOCK_MV      3300
#define V2_POWER_SHUTDOWN_SAMPLES 3
#define V2_POWER_WORSE_SAMPLES    2
#define V2_POWER_BETTER_SAMPLES   4

uint8_t SolarGuardian::classify(uint16_t mv, bool external) const {
  if (external) return NORMAL;
  if (mv < 1000) return state;  // reject invalid ADC reads
  if (mv <= V2_POWER_CRITICAL_MV) return CRITICAL;
  if (mv < V2_POWER_SURVIVAL_MV) return SURVIVAL;
  if (mv < V2_POWER_ECO_MV) return ECO;
  return NORMAL;
}

void SolarGuardian::applyPolicy() {
  static const float factors[] = {9.0f, 19.0f, 49.0f, 99.0f};
  const bool suppress_background = state >= SURVIVAL;
  mesh.setV2PowerPolicy(factors[state], suppress_background, state, last_mv);
}

void SolarGuardian::begin() {
  last_mv = board.getBattMilliVolts();
  state = classify(last_mv, board.isExternalPowered());
  applyPolicy();
  next_sample = millis() + V2_POWER_SAMPLE_MS;
}

void SolarGuardian::loop() {
  const uint32_t now = millis();
  if ((int32_t)(now - next_sample) < 0) return;
  next_sample = now + V2_POWER_SAMPLE_MS;

  const bool external = board.isExternalPowered();
  const uint16_t mv = board.getBattMilliVolts();
  if (mv > 1000) last_mv = mv;

  const uint8_t target = classify(last_mv, external);

  if (target > state) {
    better_samples = 0;
    if (++worse_samples >= V2_POWER_WORSE_SAMPLES) {
      state = target;
      worse_samples = 0;
    }
  } else if (target < state) {
    worse_samples = 0;
    if (++better_samples >= V2_POWER_BETTER_SAMPLES) {
      state--;
      better_samples = 0;
    }
  } else {
    worse_samples = 0;
    better_samples = 0;
  }

  if (!external && last_mv > 1000 && last_mv <= V2_POWER_BOOTLOCK_MV) {
    if (++shutdown_samples >= V2_POWER_SHUTDOWN_SAMPLES) {
      applyPolicy();
      board.enterV2LowVoltageProtection(); // SYSTEMOFF + LPCOMP/VBUS wake
      return;
    }
  } else {
    shutdown_samples = 0;
  }

  applyPolicy();
}
