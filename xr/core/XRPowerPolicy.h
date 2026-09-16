#pragma once

#include <cstdint>

namespace meshoffgrid::xr {

enum class XRPowerMode : uint8_t { NORMAL = 0, CONSERVE, CRITICAL, EXTERNAL_POWER };

struct XRPowerInputs {
    uint8_t batteryPercent = 100;
    bool externalPower = false;
    bool rangeSessionActive = false;
    bool userSelectedMaxRange = false;
    bool ownPendingMessages = false;
    bool carryingThirdPartyCourier = false;
};

struct XRPowerDecision {
    XRPowerMode mode = XRPowerMode::NORMAL;
    bool allowContinuousRx = true;
    bool allowRxBoost = true;
    bool allowNewThirdPartyCourier = true;
    bool allowProximityBulkTransfer = true;
    bool allowOptionalTelemetry = true;
    bool allowBackgroundGpsTracking = true;
    bool allowXrBackgroundServices = true;
    bool warnRangeBatteryTradeoff = false;
    bool warnCriticalBattery = false;
};

class XRPowerPolicy {
  public:
    XRPowerDecision evaluate(const XRPowerInputs &in) const;

  private:
    static constexpr uint8_t CONSERVE_AT_PERCENT = 30;
    static constexpr uint8_t CRITICAL_AT_PERCENT = 12;
};

} // namespace meshoffgrid::xr
