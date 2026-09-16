#pragma once

#include <cstdint>

namespace meshoffgrid::xr {

struct XRRadioPolicyState {
    bool rangeSessionActive = false;
    bool requestContinuousRx = false;
    bool requestRxBoost = false;
    bool fastReturnToRxAfterTx = false;
    bool externalPower = false;
    uint8_t batteryPercent = 100;
};

class XRRadioPolicy {
  public:
    void update(const XRRadioPolicyState &state) { state_ = state; }
    void clear() { state_ = XRRadioPolicyState{}; }
    bool useContinuousRx() const { return state_.rangeSessionActive && state_.requestContinuousRx; }
    bool requestRxBoost() const { return state_.rangeSessionActive && state_.requestRxBoost; }
    bool fastReturnToRxAfterTx() const { return state_.rangeSessionActive && state_.fastReturnToRxAfterTx; }
    const XRRadioPolicyState &state() const { return state_; }

  private:
    XRRadioPolicyState state_{};
};

} // namespace meshoffgrid::xr
