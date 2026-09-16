#include "XRPowerPolicy.h"

namespace meshoffgrid::xr {

XRPowerDecision XRPowerPolicy::evaluate(const XRPowerInputs &in) const
{
    XRPowerDecision out{};

    if (in.externalPower) {
        out.mode = XRPowerMode::EXTERNAL_POWER;
        return out;
    }

    if (in.batteryPercent <= CRITICAL_AT_PERCENT) {
        out.mode = XRPowerMode::CRITICAL;
        out.allowNewThirdPartyCourier = false;
        out.allowProximityBulkTransfer = false;
        out.allowOptionalTelemetry = false;
        out.allowBackgroundGpsTracking = false;
        out.allowXrBackgroundServices = false;
        out.warnCriticalBattery = true;
        out.allowContinuousRx = in.rangeSessionActive && (in.ownPendingMessages || in.userSelectedMaxRange);
        out.allowRxBoost = in.rangeSessionActive;
        out.warnRangeBatteryTradeoff = out.allowContinuousRx;
        return out;
    }

    if (in.batteryPercent <= CONSERVE_AT_PERCENT) {
        out.mode = XRPowerMode::CONSERVE;
        out.allowNewThirdPartyCourier = false;
        out.allowProximityBulkTransfer = false;
        out.allowOptionalTelemetry = false;
        out.allowBackgroundGpsTracking = false;
        out.allowXrBackgroundServices = false;
        out.allowContinuousRx = in.rangeSessionActive && (in.ownPendingMessages || in.userSelectedMaxRange);
        out.allowRxBoost = in.rangeSessionActive;
        out.warnRangeBatteryTradeoff = out.allowContinuousRx;
        return out;
    }

    out.mode = XRPowerMode::NORMAL;
    out.allowContinuousRx = in.rangeSessionActive;
    out.allowRxBoost = in.rangeSessionActive;
    out.allowNewThirdPartyCourier = true;
    out.allowProximityBulkTransfer = true;
    out.allowOptionalTelemetry = !in.rangeSessionActive;
    out.allowBackgroundGpsTracking = !in.rangeSessionActive;
    out.allowXrBackgroundServices = !in.rangeSessionActive;
    out.warnRangeBatteryTradeoff = in.rangeSessionActive && in.userSelectedMaxRange;
    return out;
}

} // namespace meshoffgrid::xr
