#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xr {

enum class XRFaultClass : uint8_t {
    NONE = 0,
    TEMPORARY_CONGESTION,
    BROAD_LINK_DEGRADATION,
    FREQUENCY_STABILITY_SUSPECT,
    ASYMMETRIC_LINK,
    PEER_SPECIFIC_LOSS,
};

struct XRHealthObservation {
    uint32_t peerKey = 0;
    int16_t rssiDbm = -127;
    int8_t snrDb = -20;
    int32_t frequencyErrorHz = 0;
    uint8_t channelHealth = 50;
    bool packetReceived = false;
    bool ackReceived = false;
    bool directPath = false;
};

struct XRSelfHealingDecision {
    XRFaultClass fault = XRFaultClass::NONE;
    uint8_t confidence = 0;

    bool requestRxFocus = false;
    bool suppressOptionalBackground = false;
    bool requestRadioReinit = false;
    bool recommendShortBackoff = false;
    bool preferAlternateTransport = false;
};

class XRSelfHealingMonitor {
  public:
    static constexpr size_t WINDOW = 16;

    void observe(const XRHealthObservation &observation, uint32_t nowMs);
    XRSelfHealingDecision diagnose(uint32_t nowMs) const;

  private:
    struct Sample {
        XRHealthObservation observation{};
        uint32_t timeMs = 0;
        bool used = false;
    };

    std::array<Sample, WINDOW> samples_{};
    size_t writeIndex_ = 0;

    static int32_t abs32(int32_t value) { return value < 0 ? -value : value; }
};

} // namespace meshoffgrid::xr
