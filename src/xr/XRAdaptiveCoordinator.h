#pragma once

#include "XRAdaptiveIntelligence.h"
#include "XRAdaptiveStore.h"

namespace meshoffgrid::xr {

struct XRAdaptivePlan {
    XRAdaptiveDecision decision{};

    bool preferLoRa = false;
    bool preferWifiMqtt = false;
    bool requestRxFocus = false;
    bool suppressOptionalBackground = false;
    bool requestRecoveryWindow = false;
    bool deferForCourier = false;
};

class XRAdaptiveCoordinator {
  public:
    explicit XRAdaptiveCoordinator(const XRAdaptivePolicy &policy = XRAdaptivePolicy{}) : intelligence_(policy) {}

    // Call after the normal Meshtastic filesystem has been initialized.
    void begin() { XRAdaptiveStore::load(intelligence_); }

    // Produces an invisible optimization plan. The caller remains responsible
    // for applying only the hints supported by the current transport/radio state.
    XRAdaptivePlan plan(const XRAdaptiveContext &context, const XRAdaptiveCapabilities &capabilities,
                        uint32_t nowMs, uint32_t decisionNonce = 0) const;

    // Feed the real result back into the learner. The next decision immediately
    // benefits from the new evidence; persistence is intentionally rate-limited.
    void report(const XRAdaptivePlan &plan, const XRAdaptiveOutcome &outcome, uint32_t nowMs);

    // Cheap background maintenance; safe to call periodically from an existing
    // scheduler/thread. No UI interaction is required.
    bool service(uint32_t nowMs) { return XRAdaptiveStore::save(intelligence_, nowMs); }

    XRAdaptiveIntelligence &intelligence() { return intelligence_; }
    const XRAdaptiveIntelligence &intelligence() const { return intelligence_; }

  private:
    XRAdaptiveIntelligence intelligence_;
};

} // namespace meshoffgrid::xr
