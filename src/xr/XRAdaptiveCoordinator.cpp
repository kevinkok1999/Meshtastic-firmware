#include "XRAdaptiveCoordinator.h"

namespace meshoffgrid::xr {

XRAdaptivePlan XRAdaptiveCoordinator::plan(const XRAdaptiveContext &context,
                                           const XRAdaptiveCapabilities &capabilities, uint32_t nowMs,
                                           uint32_t decisionNonce) const
{
    XRAdaptivePlan out{};
    out.decision = intelligence_.choose(context, capabilities, nowMs, decisionNonce);

    switch (out.decision.action) {
    case XRAdaptiveAction::BASELINE:
        break;
    case XRAdaptiveAction::LORA_PREFERRED:
        out.preferLoRa = true;
        break;
    case XRAdaptiveAction::WIFI_MQTT_PREFERRED:
        out.preferWifiMqtt = true;
        break;
    case XRAdaptiveAction::LORA_RX_FOCUS:
        out.preferLoRa = true;
        out.requestRxFocus = true;
        break;
    case XRAdaptiveAction::QUIET_BACKGROUND:
        out.suppressOptionalBackground = true;
        break;
    case XRAdaptiveAction::RECOVERY_WINDOW:
        out.preferLoRa = true;
        out.requestRecoveryWindow = true;
        break;
    case XRAdaptiveAction::COURIER_WAIT:
        out.deferForCourier = true;
        break;
    case XRAdaptiveAction::COUNT:
        break;
    }

    return out;
}

void XRAdaptiveCoordinator::report(const XRAdaptivePlan &plan, const XRAdaptiveOutcome &outcome, uint32_t nowMs)
{
    intelligence_.learn(plan.decision, outcome, nowMs);
}

} // namespace meshoffgrid::xr
