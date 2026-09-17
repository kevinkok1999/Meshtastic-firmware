#pragma once

#include "mesh/generated/meshtastic/mesh.pb.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xr {

// Bounded in-RAM store/carry/forward queue for already-encrypted Meshtastic packets.
// The queue intentionally stores the complete MeshPacket so the original packet ID
// survives later delivery and normal Meshtastic duplicate suppression still applies.
class XRDeferredPacketQueue
{
  public:
    static constexpr size_t MAX_ENTRIES = 12;
    static constexpr uint32_t DEFAULT_TTL_MS = 24u * 60u * 60u * 1000u;
    static constexpr uint32_t RELIABLE_INITIAL_GRACE_MS = 8000u;
    static constexpr uint32_t CLAIM_LEASE_MS = 5000u;
    static constexpr uint32_t ACK_WAIT_MS = 15000u;

    struct Entry {
        bool used = false;
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        uint32_t queuedAtMs = 0;
        uint32_t nextAttemptMs = 0;
        uint8_t attempts = 0;
        uint8_t failureStreak = 0;
    };

    bool enqueue(const meshtastic_MeshPacket &packet, uint32_t nowMs);
    void expire(uint32_t nowMs, uint32_t ttlMs = DEFAULT_TTL_MS);

    Entry *entry(size_t index);
    const Entry *entry(size_t index) const;

    // Remove only after an end-to-end ACK (or explicit cancellation).
    void markDelivered(uint32_t packetId, uint32_t destination);

    // The sidecar accepted the full carrier packet. This is not end-to-end
    // delivery, so keep it queued while giving the remote ACK time to return.
    void markTransportAccepted(uint32_t packetId, uint32_t destination, uint32_t nowMs);

    // Local transport attempt failed before the full carrier was accepted.
    void markFailure(uint32_t packetId, uint32_t destination, uint32_t nowMs);

    // Compatibility alias for older callers: success means end-to-end success.
    void markSuccess(uint32_t packetId, uint32_t destination) { markDelivered(packetId, destination); }

    size_t size() const;
    bool empty() const { return size() == 0; }
    void clear();

    uint32_t generation() const { return generation_; }
    void markPersisted() { persistedGeneration_ = generation_; }
    bool dirty() const { return generation_ != persistedGeneration_; }

  private:
    std::array<Entry, MAX_ENTRIES> entries_{};
    uint32_t generation_ = 0;
    uint32_t persistedGeneration_ = 0;

    Entry *find(uint32_t packetId, uint32_t destination);
    Entry *allocate(uint32_t nowMs);
    static uint32_t retryDelayMs(uint8_t failureStreak);
};

} // namespace meshoffgrid::xr
