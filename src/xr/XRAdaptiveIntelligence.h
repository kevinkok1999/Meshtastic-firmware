#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xr {

// XR Adaptive Intelligence never receives raw hardware-control authority.
// A lower layer exposes only approved capabilities, so the learner can improve
// behavior continuously without modifying protected platform constraints,
// cryptography, Meshtastic wire compatibility, or code.
enum class XRAdaptiveAction : uint8_t {
    BASELINE = 0,
    LORA_PREFERRED,
    WIFI_MQTT_PREFERRED,
    ESP_NOW_PREFERRED,
    XBEE_PREFERRED,
    LORA_RX_FOCUS,
    QUIET_BACKGROUND,
    RECOVERY_WINDOW,
    COURIER_WAIT,
    COUNT,
};

struct XRAdaptiveCapabilities {
    // These are already-vetted capabilities. The learner sees only whether an
    // action is available, never the low-level controls behind it.
    bool loraAvailable = true;
    bool wifiMqttAvailable = false;
    bool wifiMqttPrivacyApproved = false;
    bool espNowAvailable = false;
    bool espNowPrivacyApproved = false;
    bool xbeeAvailable = false;
    bool xbeePrivacyApproved = false;
    bool rxFocusAvailable = false;
    bool recoveryWindowAvailable = false;
    bool courierAvailable = false;
};

struct XRAdaptiveContext {
    // 0 = unknown/very poor, 100 = excellent.
    uint8_t rfLinkScore = 0;
    uint8_t channelHealthScore = 50;
    uint8_t networkAutopilotScore = 0;
    uint8_t espNowLinkScore = 0;
    uint8_t xbeeLinkScore = 0;
    uint8_t batteryPercent = 100;

    uint8_t recentAckFailures = 0;
    uint8_t recentDeliveryFailures = 0;
    uint32_t pendingAgeMs = 0;

    bool privatePayload = true;
    bool peerSeenRecently = false;
    bool directMessage = true;
};

struct XRAdaptiveOutcome {
    // delivered/acked are end-to-end message results. transportAccepted is a
    // weaker observation used by sidecar transports such as ESP-NOW: it means
    // the remote transport endpoint received and validated the carrier frame,
    // not that the Meshtastic destination has produced its normal ACK yet.
    bool delivered = false;
    bool transportAccepted = false;
    bool acked = false;
    bool duplicateObserved = false;
    bool privacyRejected = false;
    bool transportFailed = false;

    uint16_t latencyMs = 0;
    uint16_t airtimeMs = 0;
    uint16_t estimatedEnergyMilliJoules = 0;
    uint8_t retries = 0;
};

struct XRAdaptivePolicy {
    bool enabled = true;

    // Production behavior is intentionally invisible to ordinary users: the
    // model learns and applies proven improvements in the background without
    // routine prompts or popups. Advanced diagnostics may still inspect it.
    bool silentBackgroundLearning = true;
    bool autoApplyPromotedStrategies = true;
    bool exposeRoutineDecisionsToUi = false;

    // Conservative exploration keeps learning alive while the baseline remains
    // the default until another strategy has enough fresh evidence.
    uint8_t explorationPercent = 5;
    uint8_t minimumSamplesForPromotion = 8;
    int16_t minimumRewardImprovement = 8;

    uint8_t rollbackFailureStreak = 3;
    uint32_t quarantineMs = 10u * 60u * 1000u;

    uint8_t minimumBatteryForWifi = 18;
    uint8_t minimumBatteryForEspNow = 10;
    uint8_t minimumBatteryForXBee = 12;
    uint8_t minimumBatteryForRxFocus = 12;
    uint8_t minimumBatteryForCourier = 15;

    // Old evidence must stop dominating after conditions change. A promoted
    // strategy must have fresh evidence inside this window or it returns to
    // baseline/exploration until it proves itself again.
    uint32_t knowledgeFreshMs = 24u * 60u * 60u * 1000u;

    // Persistence is rate-limited by the coordinator to avoid flash wear.
    uint32_t minimumPersistIntervalMs = 60u * 60u * 1000u;
};

struct XRAdaptiveDecision {
    XRAdaptiveAction action = XRAdaptiveAction::BASELINE;
    uint8_t contextBucket = 0;
    int16_t expectedReward = 0;
    bool exploratory = false;
    bool promoted = false;
};

struct XRAdaptiveArmState {
    uint16_t samples = 0;
    uint16_t deliveries = 0;
    uint16_t failures = 0;
    int16_t rewardEwma = 0;
    uint8_t failureStreak = 0;
    uint32_t quarantineUntilMs = 0;
    uint32_t lastUpdateMs = 0;
};

class XRAdaptiveIntelligence {
  public:
    static constexpr uint8_t RF_BUCKETS = 3;      // weak / medium / strong
    static constexpr uint8_t WIFI_BUCKETS = 2;    // unavailable / available
    static constexpr uint8_t BATTERY_BUCKETS = 2; // low / normal
    static constexpr uint8_t AGE_BUCKETS = 2;     // fresh / aged pending message
    static constexpr uint8_t CONTEXT_BUCKETS = RF_BUCKETS * WIFI_BUCKETS * BATTERY_BUCKETS * AGE_BUCKETS;
    static constexpr size_t ACTION_COUNT = static_cast<size_t>(XRAdaptiveAction::COUNT);

    explicit XRAdaptiveIntelligence(const XRAdaptivePolicy &policy = XRAdaptivePolicy{});

    void setPolicy(const XRAdaptivePolicy &policy) { policy_ = policy; }
    const XRAdaptivePolicy &policy() const { return policy_; }

    XRAdaptiveDecision choose(const XRAdaptiveContext &context, const XRAdaptiveCapabilities &capabilities,
                              uint32_t nowMs, uint32_t decisionNonce = 0) const;

    void learn(const XRAdaptiveDecision &decision, const XRAdaptiveOutcome &outcome, uint32_t nowMs);

    const XRAdaptiveArmState &state(uint8_t contextBucket, XRAdaptiveAction action) const;

    // Reward is intentionally explainable and bounded. Privacy is a hard gate,
    // not a tradeable reward dimension.
    static int16_t scoreOutcome(const XRAdaptiveOutcome &outcome);

    // Fixed-size snapshot suitable for journaling in the XR no-SD storage layer.
    // The checksum detects torn/corrupt model records; it is not cryptographic.
    struct Snapshot {
        uint32_t magic = 0x58524149; // "XRAI"
        uint16_t version = 4;
        uint16_t reserved = 0;
        uint32_t modelEpoch = 0;
        std::array<std::array<XRAdaptiveArmState, ACTION_COUNT>, CONTEXT_BUCKETS> arms{};
        uint32_t checksum = 0;
    };

    Snapshot snapshot() const;
    bool restore(const Snapshot &snapshot);
    bool shouldPersist(uint32_t nowMs) const;
    void markPersisted(uint32_t nowMs)
    {
        lastPersistMs_ = nowMs;
        dirty_ = false;
    }
    bool dirty() const { return dirty_; }
    uint32_t modelEpoch() const { return modelEpoch_; }

  private:
    XRAdaptivePolicy policy_;
    std::array<std::array<XRAdaptiveArmState, ACTION_COUNT>, CONTEXT_BUCKETS> arms_{};
    uint32_t modelEpoch_ = 0;
    uint32_t lastPersistMs_ = 0;
    bool dirty_ = false;

    uint8_t bucketFor(const XRAdaptiveContext &context, const XRAdaptiveCapabilities &capabilities) const;
    bool actionAllowed(XRAdaptiveAction action, const XRAdaptiveContext &context,
                       const XRAdaptiveCapabilities &capabilities, uint32_t nowMs, uint8_t bucket) const;
    bool isPromoted(uint8_t bucket, XRAdaptiveAction action, uint32_t nowMs) const;
    bool evidenceFresh(const XRAdaptiveArmState &state, uint32_t nowMs) const;
    static uint8_t actionIndex(XRAdaptiveAction action) { return static_cast<uint8_t>(action); }
    static uint32_t checksumSnapshot(const Snapshot &snapshot);
    static uint32_t mix32(uint32_t x);
};

} // namespace meshoffgrid::xr
