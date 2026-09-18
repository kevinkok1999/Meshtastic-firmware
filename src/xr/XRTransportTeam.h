#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xr {

enum class XRTeamTransport : uint8_t {
    None = 0,
    EspNow = 1,
    XBee = 2,
};

enum class XRTeamRouteKind : uint8_t {
    Direct = 0,
    Bridge = 1,
};

// Shared delivery arbiter for XR sidecar transports.
//
// LoRa/Meshtastic remains the primary network and owns end-to-end ACK semantics.
// ESP-NOW and XBee report their currently usable routes here. For each packet the
// arbiter allows at most one secondary transport to assist/recover at a time,
// then rotates to another healthy path after failure or an ACK timeout.
class XRTransportTeam
{
  public:
    static constexpr size_t MAX_ROUTES = 48;
    static constexpr size_t MAX_PACKETS = 20;
    static constexpr size_t MAX_DESTINATIONS = 24;

    static constexpr uint32_t ROUTE_REPORT_TTL_MS = 70000u;
    static constexpr uint32_t ENVIRONMENT_TTL_MS = 10000u;
    static constexpr uint32_t PREFERRED_PATH_HOLD_MS = 45000u;
    static constexpr uint32_t ASSIST_RESERVATION_MS = 30000u;
    static constexpr uint32_t RECOVERY_ARBITRATION_MS = 150u;
    static constexpr uint32_t RECOVERY_LEASE_MS = 8000u;
    static constexpr uint32_t ACK_WAIT_MS = 60000u;
    static constexpr uint32_t FAILED_TRANSPORT_COOLDOWN_MS = 5000u;
    static constexpr uint32_t POST_ACCEPT_SWITCH_MS = 10000u;

    static XRTransportTeam &shared();

    // RF environment is advisory only: unknown ambient RF is never treated as
    // a carrier. It can only make route selection more conservative.
    void reportEnvironment(uint8_t batteryPercent, uint8_t channelUtilizationPercent, int16_t noiseFloorDbm,
                           uint32_t nowMs);

    void reportRoute(XRTeamTransport transport, uint32_t destination, uint8_t score, bool available, uint32_t nowMs,
                     XRTeamRouteKind kind = XRTeamRouteKind::Direct);

    // Called before a best-effort secondary copy is sent alongside the normal
    // LoRa attempt. Exactly one currently preferred sidecar gets the reservation.
    bool allowAssist(XRTeamTransport transport, uint32_t destination, uint32_t packetId, uint32_t nowMs);
    void reportAssistResult(XRTeamTransport transport, uint32_t destination, uint32_t packetId, bool accepted,
                            uint32_t nowMs);

    // Called for persistent recovery after reliable LoRa has exhausted retries.
    // A short lease prevents ESP-NOW and XBee from transmitting the same packet
    // simultaneously. A successful carrier handoff starts an end-to-end ACK wait.
    bool claimRecovery(XRTeamTransport transport, uint32_t destination, uint32_t packetId, uint32_t nowMs);
    void reportRecoveryResult(XRTeamTransport transport, uint32_t destination, uint32_t packetId, bool accepted,
                              uint32_t nowMs);

    // End-to-end Meshtastic result. ACK/NAK is authoritative for every transport.
    void markDelivered(uint32_t destination, uint32_t packetId, uint32_t nowMs);
    void markCancelled(uint32_t destination, uint32_t packetId, uint32_t nowMs);

    XRTeamTransport preferredTransport(uint32_t destination, uint32_t nowMs);

    // Primarily for deterministic native tests.
    void reset();

  private:
    struct RouteState {
        bool used = false;
        XRTeamTransport transport = XRTeamTransport::None;
        XRTeamRouteKind kind = XRTeamRouteKind::Direct;
        uint32_t destination = 0;
        uint8_t score = 0;
        bool available = false;
        uint32_t reportedAtMs = 0;
    };

    struct PacketState {
        bool used = false;
        uint32_t destination = 0;
        uint32_t packetId = 0;
        uint32_t lastTouchedMs = 0;

        XRTeamTransport assistOwner = XRTeamTransport::None;
        uint32_t assistUntilMs = 0;

        bool recoveryArbitrated = false;
        uint32_t recoveryArbitrationUntilMs = 0;
        XRTeamTransport recoveryOwner = XRTeamTransport::None;
        uint32_t recoveryLeaseUntilMs = 0;

        XRTeamTransport lastAccepted = XRTeamTransport::None;
        bool lastAcceptedWasRecovery = false;
        uint32_t ackWaitUntilMs = 0;

        uint32_t espNowCooldownUntilMs = 0;
        uint32_t xbeeCooldownUntilMs = 0;
    };

    struct DestinationMemory {
        bool used = false;
        uint32_t destination = 0;
        int16_t espNowQuality = 50;
        int16_t xbeeQuality = 50;
        uint16_t espNowSamples = 0;
        uint16_t xbeeSamples = 0;
        XRTeamTransport preferred = XRTeamTransport::None;
        uint32_t preferredUntilMs = 0;
        uint32_t lastTouchedMs = 0;
    };

    struct EnvironmentState {
        uint8_t batteryPercent = 100;
        uint8_t channelUtilizationPercent = 0;
        int16_t noiseFloorDbm = -120;
        uint32_t reportedAtMs = 0;
    };

    std::array<RouteState, MAX_ROUTES> routes_{};
    std::array<PacketState, MAX_PACKETS> packets_{};
    std::array<DestinationMemory, MAX_DESTINATIONS> destinations_{};
    EnvironmentState environment_{};
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;

    void lock();
    void unlock();

    RouteState *findRoute(XRTeamTransport transport, uint32_t destination);
    RouteState *allocateRoute(uint32_t nowMs);
    PacketState *findPacket(uint32_t destination, uint32_t packetId);
    PacketState *allocatePacket(uint32_t destination, uint32_t packetId, uint32_t nowMs);
    DestinationMemory *findDestination(uint32_t destination);
    const DestinationMemory *findDestination(uint32_t destination) const;
    DestinationMemory *allocateDestination(uint32_t destination, uint32_t nowMs);
    void updateQualityUnlocked(uint32_t destination, XRTeamTransport transport, uint8_t sample, uint32_t nowMs);
    int16_t qualityForUnlocked(uint32_t destination, XRTeamTransport transport) const;

    XRTeamTransport selectBestUnlocked(uint32_t destination, uint32_t nowMs, const PacketState *packet) const;
    static bool deadlinePending(uint32_t nowMs, uint32_t deadlineMs);
    static uint32_t &cooldownFor(PacketState &packet, XRTeamTransport transport);
    static int clampScore(int value, int minValue, int maxValue);
};

} // namespace meshoffgrid::xr
