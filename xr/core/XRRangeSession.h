#pragma once

#include <cstdint>

namespace meshoffgrid::xr {

enum class RangeSessionState : uint8_t { IDLE = 0, ARMED, ACTIVE, WAITING_FOR_ACK, RECOVERY };
enum class RangeSessionProfile : uint8_t { STOCK_LONG_SLOW = 0, XR_RANGE_CANDIDATE, XR_MAX_RANGE_CANDIDATE };
enum class RangeTrafficClass : uint8_t {
    DIRECTED_TEXT = 0,
    ROUTING_ACK,
    REQUIRED_MESH_CONTROL,
    USER_LOCATION_SHARE,
    AUTOMATIC_POSITION,
    NODE_INFO,
    OPTIONAL_TELEMETRY,
    XR_BACKGROUND_SERVICE,
};

struct RangeSessionPolicy {
    bool continuousRx = true;
    bool rxBoostRequested = true;
    bool suppressOptionalTelemetry = true;
    bool suppressAutomaticPosition = true;
    bool suppressNonessentialDiscovery = true;
    bool suppressXrBackgroundServices = true;
    bool fastReturnToRxAfterTx = true;
    bool prioritizeDirectedText = true;
    bool collectFrequencyError = true;
    bool collectNoiseFloor = true;
    bool collectAckAsymmetry = true;
    uint32_t peerFreshWindowMs = 90000;
    uint32_t maxSessionIdleMs = 10u * 60u * 1000u;
};

struct RangeSessionMetrics {
    uint32_t txMessages = 0;
    uint32_t deliveredMessages = 0;
    uint32_t ackFailures = 0;
    uint32_t receivedPeerPackets = 0;
    int16_t lastRssiDbm = -140;
    float lastSnrDb = -30.0f;
    float frequencyErrorHz = 0.0f;
    int16_t noiseFloorDbm = -140;
    uint8_t channelUtilization = 0;
};

class XRRangeSession {
  public:
    explicit XRRangeSession(const RangeSessionPolicy &policy = RangeSessionPolicy{});
    void arm(uint32_t peerNode, uint32_t nowMs);
    void activate(RangeSessionProfile profile, uint32_t nowMs);
    void stop();
    void onDirectedTextStarted(uint32_t nowMs);
    void onDeliveryConfirmed(uint32_t nowMs);
    void onFinalAckFailure(uint32_t nowMs);
    void onPeerPacket(int16_t rssiDbm, float snrDb, float frequencyErrorHz, uint32_t nowMs);
    void updateChannel(int16_t noiseFloorDbm, uint8_t utilization);
    bool shouldKeepReceiverHot(uint32_t nowMs) const;
    bool shouldRequestRxBoost(uint32_t nowMs) const;
    bool shouldFastReturnToRx(uint32_t nowMs) const;
    bool allowsTraffic(RangeTrafficClass traffic, uint32_t nowMs) const;
    bool peerFresh(uint32_t nowMs) const;
    bool expired(uint32_t nowMs) const;
    RangeSessionState state() const { return state_; }
    RangeSessionProfile profile() const { return profile_; }
    uint32_t peerNode() const { return peerNode_; }
    const RangeSessionMetrics &metrics() const { return metrics_; }

  private:
    static bool elapsedMoreThan(uint32_t nowMs, uint32_t thenMs, uint32_t intervalMs);
    RangeSessionPolicy policy_{};
    RangeSessionState state_ = RangeSessionState::IDLE;
    RangeSessionProfile profile_ = RangeSessionProfile::STOCK_LONG_SLOW;
    RangeSessionMetrics metrics_{};
    uint32_t peerNode_ = 0;
    uint32_t startedMs_ = 0;
    uint32_t lastActivityMs_ = 0;
    uint32_t lastPeerSeenMs_ = 0;
    bool hasSeenPeer_ = false;
};

} // namespace meshoffgrid::xr
