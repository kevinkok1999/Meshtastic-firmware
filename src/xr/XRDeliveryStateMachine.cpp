#include "XRDeliveryStateMachine.h"

namespace meshoffgrid::xr {

bool XRDeliveryStateMachine::terminal(XRDeliveryPhase phase)
{
    return phase == XRDeliveryPhase::Delivered || phase == XRDeliveryPhase::Cancelled ||
           phase == XRDeliveryPhase::Expired;
}

uint8_t XRDeliveryStateMachine::pathMask(XRDeliveryPath path)
{
    switch (path) {
    case XRDeliveryPath::LoRa:
        return 1u << 0;
    case XRDeliveryPath::EspNow:
        return 1u << 1;
    case XRDeliveryPath::XBee:
        return 1u << 2;
    case XRDeliveryPath::None:
        return 0;
    }
    return 0;
}

uint8_t XRDeliveryStateMachine::saturatingIncrement(uint8_t value)
{
    return value == UINT8_MAX ? value : static_cast<uint8_t>(value + 1u);
}

XRDeliveryStateMachine::Entry *XRDeliveryStateMachine::find(uint32_t destination, uint32_t packetId)
{
    for (auto &entry : entries_) {
        if (entry.used && entry.destination == destination && entry.packetId == packetId)
            return &entry;
    }
    return nullptr;
}

const XRDeliveryStateMachine::Entry *XRDeliveryStateMachine::find(uint32_t destination, uint32_t packetId) const
{
    for (const auto &entry : entries_) {
        if (entry.used && entry.destination == destination && entry.packetId == packetId)
            return &entry;
    }
    return nullptr;
}

XRDeliveryStateMachine::Entry *XRDeliveryStateMachine::allocate(uint32_t nowMs)
{
    for (auto &entry : entries_) {
        if (!entry.used)
            return &entry;
    }

    // Prefer reclaiming a terminal record. Otherwise evict the oldest state;
    // payload bytes are persisted elsewhere in XRDeferredPacketQueue.
    Entry *oldest = &entries_[0];
    for (auto &entry : entries_) {
        if (terminal(entry.phase))
            return &entry;
        if ((nowMs - entry.updatedAtMs) > (nowMs - oldest->updatedAtMs))
            oldest = &entry;
    }
    return oldest;
}

bool XRDeliveryStateMachine::begin(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    if (destination == 0 || packetId == 0)
        return false;

    if (Entry *existing = find(destination, packetId)) {
        if (terminal(existing->phase))
            return false;
        existing->updatedAtMs = nowMs;
        return true;
    }

    Entry *entry = allocate(nowMs);
    if (!entry)
        return false;

    *entry = {};
    entry->used = true;
    entry->destination = destination;
    entry->packetId = packetId;
    entry->phase = XRDeliveryPhase::PrimaryActive;
    entry->activePath = XRDeliveryPath::LoRa;
    entry->attemptedMask = pathMask(XRDeliveryPath::LoRa);
    entry->createdAtMs = nowMs;
    entry->updatedAtMs = nowMs;
    return true;
}

bool XRDeliveryStateMachine::markPrimaryActive(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    if (!begin(destination, packetId, nowMs))
        return false;
    Entry *entry = find(destination, packetId);
    if (!entry || terminal(entry->phase))
        return false;

    entry->phase = XRDeliveryPhase::PrimaryActive;
    entry->activePath = XRDeliveryPath::LoRa;
    entry->attemptedMask |= pathMask(XRDeliveryPath::LoRa);
    entry->updatedAtMs = nowMs;
    return true;
}

bool XRDeliveryStateMachine::markPrimaryFailed(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    if (!begin(destination, packetId, nowMs))
        return false;
    Entry *entry = find(destination, packetId);
    if (!entry || terminal(entry->phase))
        return false;

    // XRDeliveryEvents fans the same reliable-LoRa failure out to multiple
    // sidecars. Count/transition that primary failure once, not once per sink.
    if (entry->phase == XRDeliveryPhase::RecoveryQueued ||
        entry->phase == XRDeliveryPhase::SecondaryActive ||
        entry->phase == XRDeliveryPhase::CarrierAccepted) {
        entry->updatedAtMs = nowMs;
        return true;
    }

    entry->primaryFailures = saturatingIncrement(entry->primaryFailures);
    entry->phase = XRDeliveryPhase::RecoveryQueued;
    entry->activePath = XRDeliveryPath::None;
    entry->updatedAtMs = nowMs;
    return true;
}

bool XRDeliveryStateMachine::queueRecovery(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    if (!begin(destination, packetId, nowMs))
        return false;
    Entry *entry = find(destination, packetId);
    if (!entry || terminal(entry->phase))
        return false;

    entry->phase = XRDeliveryPhase::RecoveryQueued;
    entry->activePath = XRDeliveryPath::None;
    entry->updatedAtMs = nowMs;
    return true;
}

bool XRDeliveryStateMachine::canStartSecondary(uint32_t destination, uint32_t packetId, XRDeliveryPath path) const
{
    if (path != XRDeliveryPath::EspNow && path != XRDeliveryPath::XBee)
        return false;

    const Entry *entry = find(destination, packetId);
    if (!entry)
        return true;
    if (terminal(entry->phase))
        return false;

    // Only one secondary carrier may be active for this packet at a time.
    if (entry->phase == XRDeliveryPhase::SecondaryActive && entry->activePath != path)
        return false;

    // Once a carrier was accepted we wait for authoritative end-to-end result
    // before any new sidecar is allowed to start.
    if (entry->phase == XRDeliveryPhase::CarrierAccepted)
        return false;

    return true;
}

bool XRDeliveryStateMachine::startSecondary(uint32_t destination, uint32_t packetId, XRDeliveryPath path, uint32_t nowMs)
{
    if (!begin(destination, packetId, nowMs) || !canStartSecondary(destination, packetId, path))
        return false;

    Entry *entry = find(destination, packetId);
    if (!entry)
        return false;

    entry->phase = XRDeliveryPhase::SecondaryActive;
    entry->activePath = path;
    entry->attemptedMask |= pathMask(path);
    entry->secondaryAttempts = saturatingIncrement(entry->secondaryAttempts);
    entry->updatedAtMs = nowMs;
    return true;
}

bool XRDeliveryStateMachine::markCarrierResult(uint32_t destination, uint32_t packetId, XRDeliveryPath path, bool accepted,
                                               uint32_t nowMs)
{
    Entry *entry = find(destination, packetId);
    if (!entry || terminal(entry->phase))
        return false;
    if (entry->activePath != path && entry->phase == XRDeliveryPhase::SecondaryActive)
        return false;

    entry->updatedAtMs = nowMs;
    entry->activePath = XRDeliveryPath::None;

    if (accepted) {
        entry->acceptedMask |= pathMask(path);
        entry->phase = XRDeliveryPhase::CarrierAccepted;
    } else {
        entry->phase = XRDeliveryPhase::RecoveryQueued;
    }
    return true;
}

bool XRDeliveryStateMachine::markDelivered(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    Entry *entry = find(destination, packetId);
    if (!entry)
        return false;
    if (entry->phase == XRDeliveryPhase::Cancelled || entry->phase == XRDeliveryPhase::Expired)
        return false;

    entry->phase = XRDeliveryPhase::Delivered;
    entry->activePath = XRDeliveryPath::None;
    entry->updatedAtMs = nowMs;
    return true;
}

bool XRDeliveryStateMachine::markCancelled(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    Entry *entry = find(destination, packetId);
    if (!entry)
        return false;
    if (entry->phase == XRDeliveryPhase::Delivered || entry->phase == XRDeliveryPhase::Expired)
        return false;

    entry->phase = XRDeliveryPhase::Cancelled;
    entry->activePath = XRDeliveryPath::None;
    entry->updatedAtMs = nowMs;
    return true;
}

void XRDeliveryStateMachine::expire(uint32_t nowMs, uint32_t ttlMs)
{
    for (auto &entry : entries_) {
        if (!entry.used || terminal(entry.phase))
            continue;
        if ((nowMs - entry.createdAtMs) > ttlMs) {
            entry.phase = XRDeliveryPhase::Expired;
            entry.activePath = XRDeliveryPath::None;
            entry.updatedAtMs = nowMs;
        }
    }
}

void XRDeliveryStateMachine::reset()
{
    entries_ = {};
}

} // namespace meshoffgrid::xr
