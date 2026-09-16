#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xr {

// XR never treats a public Wi-Fi network as trusted transport. A network is only
// a path to an authenticated/encrypted application transport (for example MQTT
// over TLS). Captive portal authorization is never bypassed.
enum class XRNetworkAccessClass : uint8_t {
    UNKNOWN = 0,
    OPEN_NO_PORTAL,
    PREAUTHORIZED_PORTAL,
    PASSPOINT_OR_ROAMING,
    REQUIRES_USER_INTERACTION,
    BLOCKED,
};

enum class XRNetworkRejectReason : uint8_t {
    NONE = 0,
    AUTOPILOT_DISABLED,
    BATTERY_TOO_LOW,
    SIGNAL_TOO_WEAK,
    ACCESS_NOT_ALLOWED,
    PORTAL_REQUIRES_INTERACTION,
    INTERNET_UNVERIFIED,
    SECURE_TRANSPORT_UNAVAILABLE,
    BACKOFF_ACTIVE,
    RECENT_FAILURES,
};

struct XRNetworkObservation {
    uint64_t networkKey = 0; // privacy-preserving local fingerprint, not raw SSID
    int16_t rssiDbm = -127;
    uint16_t estimatedLatencyMs = 0;
    uint8_t packetLossPercent = 100;

    bool layer2Open = false;
    bool captivePortalDetected = false;
    bool priorPortalAuthorizationValid = false;
    bool passpointOrRoamingProfileMatched = false;
    bool internetVerified = false;
    bool secureMqttReachable = false;
};

struct XRNetworkHistory {
    uint64_t networkKey = 0;
    uint16_t successfulSessions = 0;
    uint16_t failedSessions = 0;
    uint16_t successfulDeliveries = 0;
    uint16_t mqttFailures = 0;
    uint16_t ewmaLatencyMs = 0;
    int16_t ewmaRssiDbm = -127;
    uint32_t lastSuccessMs = 0;
    uint32_t lastFailureMs = 0;
    uint32_t backoffUntilMs = 0;
    bool used = false;
};

struct XRNetworkAutopilotPolicy {
    bool enabled = false; // explicit one-time product-level opt-in
    bool allowOpenNoPortal = true;
    bool allowPreauthorizedPortal = true;
    bool allowPasspointOrRoaming = true;
    bool requireInternetVerification = true;
    bool requireSecureMqtt = true;

    int16_t minimumRssiDbm = -82;
    uint8_t minimumBatteryPercent = 15;
    uint8_t maxConsecutiveFailureWeight = 5;
    uint32_t baseFailureBackoffMs = 30u * 1000u;
    uint32_t maxFailureBackoffMs = 30u * 60u * 1000u;
};

struct XRNetworkDecision {
    bool connect = false;
    uint8_t score = 0;
    XRNetworkAccessClass accessClass = XRNetworkAccessClass::UNKNOWN;
    XRNetworkRejectReason rejectReason = XRNetworkRejectReason::NONE;
};

class XRNetworkAutopilot {
  public:
    static constexpr size_t MAX_NETWORK_HISTORY = 24;

    explicit XRNetworkAutopilot(const XRNetworkAutopilotPolicy &policy = XRNetworkAutopilotPolicy{});

    void setPolicy(const XRNetworkAutopilotPolicy &policy) { policy_ = policy; }
    const XRNetworkAutopilotPolicy &policy() const { return policy_; }

    XRNetworkDecision evaluate(const XRNetworkObservation &observation, uint8_t batteryPercent, uint32_t nowMs) const;

    void recordSessionResult(uint64_t networkKey, bool connected, bool internetVerified, bool secureMqttReady,
                             int16_t rssiDbm, uint16_t latencyMs, uint32_t nowMs);
    void recordDeliveryResult(uint64_t networkKey, bool delivered, uint32_t nowMs);

    const XRNetworkHistory *history(uint64_t networkKey) const;

    // Selection is deterministic and local: the highest scoring eligible network wins.
    // No cloud model and no raw SSID history are required.
    int selectBest(const XRNetworkObservation *observations, size_t count, uint8_t batteryPercent, uint32_t nowMs,
                   XRNetworkDecision *decision = nullptr) const;

  private:
    XRNetworkAutopilotPolicy policy_;
    std::array<XRNetworkHistory, MAX_NETWORK_HISTORY> history_{};

    XRNetworkHistory *findOrAllocate(uint64_t networkKey);
    const XRNetworkHistory *find(uint64_t networkKey) const;

    XRNetworkAccessClass classify(const XRNetworkObservation &observation) const;
    static uint8_t clampScore(int score);
    static uint32_t elapsed(uint32_t nowMs, uint32_t thenMs) { return nowMs - thenMs; }
};

} // namespace meshoffgrid::xr
