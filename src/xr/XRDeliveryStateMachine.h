#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xr {

enum class XRDeliveryPhase : uint8_t {
    Empty = 0,
    PrimaryActive,
    RecoveryQueued,
    SecondaryActive,
    CarrierAccepted,
    Delivered,
    Cancelled,
    Expired,
};

enum class XRDeliveryPath : uint8_t {
    None = 0,
    LoRa = 1,
    EspNow = 2,
    XBee = 3,
};

// Small deterministic state machine for a locally-originated message.
//
// It never owns payload bytes and therefore cannot leak content. Its purpose is
// to keep all delivery transports aligned on one lifecycle and to enforce the
// core invariant that a local carrier success is never equivalent to an
// end-to-end Meshtastic delivery acknowledgement.
class XRDeliveryStateMachine
{
  public:
    static constexpr size_t MAX_ENTRIES = 24;
    static constexpr uint32_t DEFAULT_TTL_MS = 24u * 60u * 60u * 1000u;

    struct Entry {
        bool used = false;
        uint32_t destination = 0;
        uint32_t packetId = 0;
        XRDeliveryPhase phase = XRDeliveryPhase::Empty;
        XRDeliveryPath activePath = XRDeliveryPath::None;
        uint8_t attemptedMask = 0;
        uint8_t acceptedMask = 0;
        uint8_t primaryFailures = 0;
        bool primaryFailed = false;
        uint8_t secondaryAttempts = 0;
        uint32_t createdAtMs = 0;
        uint32_t updatedAtMs = 0;
    };

    bool begin(uint32_t destination, uint32_t packetId, uint32_t nowMs);
    bool markPrimaryActive(uint32_t destination, uint32_t packetId, uint32_t nowMs);
    bool markPrimaryFailed(uint32_t destination, uint32_t packetId, uint32_t nowMs);
    bool queueRecovery(uint32_t destination, uint32_t packetId, uint32_t nowMs);

    bool canStartSecondary(uint32_t destination, uint32_t packetId, XRDeliveryPath path) const;
    bool startSecondary(uint32_t destination, uint32_t packetId, XRDeliveryPath path, uint32_t nowMs);
    bool markCarrierResult(uint32_t destination, uint32_t packetId, XRDeliveryPath path, bool accepted, uint32_t nowMs);

    bool markDelivered(uint32_t destination, uint32_t packetId, uint32_t nowMs);
    bool markCancelled(uint32_t destination, uint32_t packetId, uint32_t nowMs);
    void expire(uint32_t nowMs, uint32_t ttlMs = DEFAULT_TTL_MS);

    const Entry *find(uint32_t destination, uint32_t packetId) const;
    Entry *find(uint32_t destination, uint32_t packetId);
    void reset();

    static bool terminal(XRDeliveryPhase phase);

  private:
    std::array<Entry, MAX_ENTRIES> entries_{};

    Entry *allocate(uint32_t nowMs);
    static uint8_t pathMask(XRDeliveryPath path);
    static uint8_t saturatingIncrement(uint8_t value);
};

} // namespace meshoffgrid::xr
