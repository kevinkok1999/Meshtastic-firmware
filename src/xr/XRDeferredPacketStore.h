#pragma once

#include "XRDeferredPacketQueue.h"

#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xr {

// Flash persistence for the bounded deferred queue. Only already-encrypted
// Meshtastic MeshPackets are persisted. Writes are rate-limited to protect flash.
class XRDeferredPacketStore
{
  public:
    static constexpr const char *PATH = "/prefs/xr_deferred.bin";
    static constexpr const char *TEMP_PATH = "/prefs/xr_deferred.tmp";
    static constexpr uint32_t MIN_PERSIST_INTERVAL_MS = 60u * 1000u;

    explicit XRDeferredPacketStore(const char *path = PATH, const char *tempPath = TEMP_PATH)
        : path_(path != nullptr ? path : PATH), tempPath_(tempPath != nullptr ? tempPath : TEMP_PATH)
    {
    }

    bool load(XRDeferredPacketQueue &queue, uint32_t nowMs);
    bool service(XRDeferredPacketQueue &queue, uint32_t nowMs, bool force = false);

  private:
    struct Record {
        uint16_t length = 0;
        uint8_t bytes[meshtastic_MeshPacket_size]{};
    };

    struct Snapshot {
        uint32_t magic = 0x58525131; // "XRQ1"
        uint16_t version = 1;
        uint16_t count = 0;
        Record records[XRDeferredPacketQueue::MAX_ENTRIES]{};
        uint32_t checksum = 0;
    };

    const char *path_ = PATH;
    const char *tempPath_ = TEMP_PATH;
    uint32_t lastPersistMs_ = 0;

    static uint32_t checksumSnapshot(const Snapshot &snapshot);
    static bool encodeQueue(const XRDeferredPacketQueue &queue, Snapshot &snapshot);
    static bool decodeQueue(const Snapshot &snapshot, XRDeferredPacketQueue &queue, uint32_t nowMs);
};

} // namespace meshoffgrid::xr
