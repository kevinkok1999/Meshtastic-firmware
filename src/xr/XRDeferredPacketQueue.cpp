#include "XRDeferredPacketQueue.h"

#include <algorithm>

namespace meshoffgrid::xr {

XRDeferredPacketQueue::Entry *XRDeferredPacketQueue::find(uint32_t packetId, uint32_t destination)
{
    for (auto &item : entries_) {
        if (item.used && item.packet.id == packetId && item.packet.to == destination)
            return &item;
    }
    return nullptr;
}

XRDeferredPacketQueue::Entry *XRDeferredPacketQueue::allocate(uint32_t nowMs)
{
    for (auto &item : entries_) {
        if (!item.used)
            return &item;
    }

    // Fixed memory budget: evict the oldest queued packet rather than growing
    // heap usage on a handheld device.
    Entry *oldest = &entries_[0];
    for (auto &item : entries_) {
        if ((nowMs - item.queuedAtMs) > (nowMs - oldest->queuedAtMs))
            oldest = &item;
    }
    return oldest;
}

bool XRDeferredPacketQueue::enqueue(const meshtastic_MeshPacket &packet, uint32_t nowMs)
{
    if (packet.id == 0 || packet.to == 0 ||
        packet.which_payload_variant != meshtastic_MeshPacket_encrypted_tag)
        return false;

    if (Entry *existing = find(packet.id, packet.to)) {
        // Keep original age/backoff but refresh the exact packet bytes in case
        // Meshtastic updated hop/transport metadata before release.
        existing->packet = packet;
        ++generation_;
        return true;
    }

    Entry *slot = allocate(nowMs);
    if (!slot)
        return false;

    *slot = {};
    slot->used = true;
    slot->packet = packet;
    slot->queuedAtMs = nowMs;
    slot->nextAttemptMs = nowMs;
    ++generation_;
    return true;
}

void XRDeferredPacketQueue::expire(uint32_t nowMs, uint32_t ttlMs)
{
    for (auto &item : entries_) {
        if (item.used && (nowMs - item.queuedAtMs) > ttlMs) {
            item = {};
            ++generation_;
        }
    }
}

XRDeferredPacketQueue::Entry *XRDeferredPacketQueue::entry(size_t index)
{
    return index < entries_.size() ? &entries_[index] : nullptr;
}

const XRDeferredPacketQueue::Entry *XRDeferredPacketQueue::entry(size_t index) const
{
    return index < entries_.size() ? &entries_[index] : nullptr;
}

void XRDeferredPacketQueue::markSuccess(uint32_t packetId, uint32_t destination)
{
    if (Entry *item = find(packetId, destination)) {
        *item = {};
        ++generation_;
    }
}

uint32_t XRDeferredPacketQueue::retryDelayMs(uint8_t failureStreak)
{
    // Short failures retry quickly; repeated failures back off to reduce RF
    // airtime and battery use. Capped at five minutes.
    switch (std::min<uint8_t>(failureStreak, 6)) {
    case 0:
        return 1000;
    case 1:
        return 3000;
    case 2:
        return 10000;
    case 3:
        return 30000;
    case 4:
        return 60000;
    default:
        return 300000;
    }
}

void XRDeferredPacketQueue::markFailure(uint32_t packetId, uint32_t destination, uint32_t nowMs)
{
    Entry *item = find(packetId, destination);
    if (!item)
        return;

    if (item->attempts != UINT8_MAX)
        ++item->attempts;
    if (item->failureStreak != UINT8_MAX)
        ++item->failureStreak;

    item->nextAttemptMs = nowMs + retryDelayMs(item->failureStreak);
    ++generation_;
}

size_t XRDeferredPacketQueue::size() const
{
    size_t count = 0;
    for (const auto &item : entries_) {
        if (item.used)
            ++count;
    }
    return count;
}

void XRDeferredPacketQueue::clear()
{
    bool changed = false;
    for (auto &item : entries_) {
        changed = changed || item.used;
        item = {};
    }
    if (changed)
        ++generation_;
}

} // namespace meshoffgrid::xr
