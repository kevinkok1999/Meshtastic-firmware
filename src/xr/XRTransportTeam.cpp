#include "XRTransportTeam.h"

#include <algorithm>

namespace meshoffgrid::xr {

XRTransportTeam &XRTransportTeam::shared()
{
    static XRTransportTeam team;
    return team;
}

void XRTransportTeam::lock()
{
    while (lock_.test_and_set(std::memory_order_acquire)) {
    }
}

void XRTransportTeam::unlock()
{
    lock_.clear(std::memory_order_release);
}

bool XRTransportTeam::deadlinePending(uint32_t nowMs, uint32_t deadlineMs)
{
    return deadlineMs != 0 && static_cast<int32_t>(nowMs - deadlineMs) < 0;
}

uint32_t &XRTransportTeam::cooldownFor(PacketState &packet, XRTeamTransport transport)
{
    return transport == XRTeamTransport::XBee ? packet.xbeeCooldownUntilMs : packet.espNowCooldownUntilMs;
}

XRDeliveryPath XRTransportTeam::deliveryPathFor(XRTeamTransport transport)
{
    switch (transport) {
    case XRTeamTransport::EspNow:
        return XRDeliveryPath::EspNow;
    case XRTeamTransport::XBee:
        return XRDeliveryPath::XBee;
    case XRTeamTransport::None:
        return XRDeliveryPath::None;
    }
    return XRDeliveryPath::None;
}

uint32_t XRTransportTeam::checksumLearningSnapshot(const LearningSnapshot &snapshot)
{
    constexpr uint32_t FNV_OFFSET = 2166136261u;
    constexpr uint32_t FNV_PRIME = 16777619u;
    uint32_t hash = FNV_OFFSET;

    auto feed8 = [&hash](uint8_t value) {
        hash ^= value;
        hash *= FNV_PRIME;
    };
    auto feed16 = [&feed8](uint16_t value) {
        feed8(static_cast<uint8_t>(value & 0xffu));
        feed8(static_cast<uint8_t>((value >> 8) & 0xffu));
    };
    auto feed32 = [&feed8](uint32_t value) {
        for (uint8_t shift = 0; shift < 32; shift += 8)
            feed8(static_cast<uint8_t>((value >> shift) & 0xffu));
    };

    feed32(snapshot.magic);
    feed16(snapshot.version);
    feed16(snapshot.recordCount);
    for (const auto &record : snapshot.records) {
        feed8(record.used ? 1u : 0u);
        feed32(record.destination);
        feed16(static_cast<uint16_t>(record.espNowQuality));
        feed16(static_cast<uint16_t>(record.xbeeQuality));
        feed16(record.espNowSamples);
        feed16(record.xbeeSamples);
    }
    return hash;
}

int XRTransportTeam::clampScore(int value, int minValue, int maxValue)
{
    return std::max(minValue, std::min(maxValue, value));
}

XRTransportTeam::RouteState *XRTransportTeam::findRoute(XRTeamTransport transport, uint32_t destination)
{
    for (auto &route : routes_) {
        if (route.used && route.transport == transport && route.destination == destination)
            return &route;
    }
    return nullptr;
}

XRTransportTeam::RouteState *XRTransportTeam::allocateRoute(uint32_t nowMs)
{
    for (auto &route : routes_) {
        if (!route.used)
            return &route;
    }

    RouteState *oldest = &routes_[0];
    for (auto &route : routes_) {
        if ((nowMs - route.reportedAtMs) > (nowMs - oldest->reportedAtMs))
            oldest = &route;
    }
    return oldest;
}

XRTransportTeam::PacketState *XRTransportTeam::findPacket(uint32_t destination, uint32_t packetId)
{
    for (auto &packet : packets_) {
        if (packet.used && packet.destination == destination && packet.packetId == packetId)
            return &packet;
    }
    return nullptr;
}

XRTransportTeam::PacketState *XRTransportTeam::allocatePacket(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    if (PacketState *existing = findPacket(destination, packetId)) {
        existing->lastTouchedMs = nowMs;
        return existing;
    }

    PacketState *slot = nullptr;
    for (auto &packet : packets_) {
        if (!packet.used) {
            slot = &packet;
            break;
        }
        if (!slot || (nowMs - packet.lastTouchedMs) > (nowMs - slot->lastTouchedMs))
            slot = &packet;
    }

    if (!slot)
        return nullptr;

    *slot = {};
    slot->used = true;
    slot->destination = destination;
    slot->packetId = packetId;
    slot->lastTouchedMs = nowMs;
    return slot;
}

XRTransportTeam::DestinationMemory *XRTransportTeam::findDestination(uint32_t destination)
{
    for (auto &memory : destinations_) {
        if (memory.used && memory.destination == destination)
            return &memory;
    }
    return nullptr;
}

const XRTransportTeam::DestinationMemory *XRTransportTeam::findDestination(uint32_t destination) const
{
    for (const auto &memory : destinations_) {
        if (memory.used && memory.destination == destination)
            return &memory;
    }
    return nullptr;
}

XRTransportTeam::DestinationMemory *XRTransportTeam::allocateDestination(uint32_t destination, uint32_t nowMs)
{
    if (DestinationMemory *existing = findDestination(destination)) {
        existing->lastTouchedMs = nowMs;
        return existing;
    }

    DestinationMemory *slot = nullptr;
    for (auto &memory : destinations_) {
        if (!memory.used) {
            slot = &memory;
            break;
        }
        if (!slot || (nowMs - memory.lastTouchedMs) > (nowMs - slot->lastTouchedMs))
            slot = &memory;
    }

    if (!slot)
        return nullptr;

    *slot = {};
    slot->used = true;
    slot->destination = destination;
    slot->espNowQuality = 50;
    slot->xbeeQuality = 50;
    slot->lastTouchedMs = nowMs;
    return slot;
}

void XRTransportTeam::updateQualityUnlocked(uint32_t destination, XRTeamTransport transport, uint8_t sample,
                                            uint32_t nowMs)
{
    if (transport == XRTeamTransport::None)
        return;

    DestinationMemory *memory = allocateDestination(destination, nowMs);
    if (!memory)
        return;

    int16_t *quality = transport == XRTeamTransport::XBee ? &memory->xbeeQuality : &memory->espNowQuality;
    uint16_t *samples = transport == XRTeamTransport::XBee ? &memory->xbeeSamples : &memory->espNowSamples;
    const int target = clampScore(sample, 0, 100);

    if (*samples == 0)
        *quality = static_cast<int16_t>(target);
    else
        *quality = static_cast<int16_t>((*quality * 7 + target) / 8);

    if (*samples != UINT16_MAX)
        ++(*samples);
    memory->lastTouchedMs = nowMs;
    learningDirty_ = true;
}

int16_t XRTransportTeam::qualityForUnlocked(uint32_t destination, XRTeamTransport transport) const
{
    const DestinationMemory *memory = findDestination(destination);
    if (!memory || transport == XRTeamTransport::None)
        return 50;
    return transport == XRTeamTransport::XBee ? memory->xbeeQuality : memory->espNowQuality;
}

void XRTransportTeam::reportEnvironment(uint8_t batteryPercent, uint8_t channelUtilizationPercent,
                                        int16_t noiseFloorDbm, uint32_t nowMs)
{
    lock();
    environment_.batteryPercent = static_cast<uint8_t>(clampScore(batteryPercent, 0, 100));
    environment_.channelUtilizationPercent =
        static_cast<uint8_t>(clampScore(channelUtilizationPercent, 0, 100));
    environment_.noiseFloorDbm = static_cast<int16_t>(clampScore(noiseFloorDbm, -127, -20));
    environment_.reportedAtMs = nowMs;
    unlock();
}

void XRTransportTeam::reportRoute(XRTeamTransport transport, uint32_t destination, uint8_t score, bool available,
                                  uint32_t nowMs, XRTeamRouteKind kind)
{
    if (transport == XRTeamTransport::None || destination == 0)
        return;

    lock();
    RouteState *route = findRoute(transport, destination);
    if (!route)
        route = allocateRoute(nowMs);

    if (route) {
        *route = {};
        route->used = true;
        route->transport = transport;
        route->kind = kind;
        route->destination = destination;
        route->score = score;
        route->available = available && score != 0;
        route->reportedAtMs = nowMs;
    }
    unlock();
}

XRTeamTransport XRTransportTeam::selectBestUnlocked(uint32_t destination, uint32_t nowMs, const PacketState *packet) const
{
    XRTeamTransport best = XRTeamTransport::None;
    int bestScore = -1;

    for (const auto &route : routes_) {
        if (!route.used || !route.available || route.destination != destination || route.score == 0)
            continue;
        if ((nowMs - route.reportedAtMs) > ROUTE_REPORT_TTL_MS)
            continue;

        if (packet) {
            const uint32_t cooldown = route.transport == XRTeamTransport::XBee ? packet->xbeeCooldownUntilMs
                                                                               : packet->espNowCooldownUntilMs;
            if (deadlinePending(nowMs, cooldown))
                continue;
        }

        // Raw radio score is only one signal. Add bounded, per-destination
        // experience plus hysteresis so rapidly changing RF does not make the
        // device thrash between transports. Energy is deliberately NOT part of
        // this quality score; battery is allowed to break ties only later.
        int effective = static_cast<int>(route.score);
        effective += (static_cast<int>(qualityForUnlocked(destination, route.transport)) - 50) / 5;
        if (route.kind == XRTeamRouteKind::Direct)
            effective += 3;
        else
            effective -= 1;

        const DestinationMemory *memory = findDestination(destination);
        if (memory && memory->preferred == route.transport && deadlinePending(nowMs, memory->preferredUntilMs))
            effective += 7;

        // Ambient RF is never reused as a carrier. It only informs how cautious
        // we should be. Bridge routes eventually depend on LoRa again, so heavy
        // 868 MHz congestion/noise makes a direct sidecar route comparatively
        // more attractive.
        if (environment_.reportedAtMs != 0 && (nowMs - environment_.reportedAtMs) <= ENVIRONMENT_TTL_MS) {
            if (route.kind == XRTeamRouteKind::Bridge) {
                if (environment_.channelUtilizationPercent >= 35)
                    effective -= 6;
                if (environment_.noiseFloorDbm > -92)
                    effective -= 5;
            } else {
                if (environment_.channelUtilizationPercent >= 35)
                    effective += 3;
            }
        }

        effective = clampScore(effective, 0, 120);

        const bool lowBattery = environment_.reportedAtMs != 0 &&
                                (nowMs - environment_.reportedAtMs) <= ENVIRONMENT_TTL_MS &&
                                environment_.batteryPercent < 15;

        if (best == XRTeamTransport::None || effective > bestScore + QUALITY_EQUIVALENCE_MARGIN) {
            bestScore = effective;
            best = route.transport;
            continue;
        }

        if (bestScore > effective + QUALITY_EQUIVALENCE_MARGIN)
            continue;

        // Within a tiny quality band the paths are treated as effectively
        // equivalent. Only here may low-battery mode prefer the lower-cost
        // integrated ESP-NOW radio over the optional external XBee radio.
        if (lowBattery) {
            const int candidateEnergyRank = route.transport == XRTeamTransport::EspNow ? 0 : 1;
            const int bestEnergyRank = best == XRTeamTransport::EspNow ? 0 : 1;
            if (candidateEnergyRank < bestEnergyRank) {
                bestScore = effective;
                best = route.transport;
                continue;
            }
            if (candidateEnergyRank > bestEnergyRank)
                continue;
        }

        // Quality still wins inside the equivalence band whenever there is a
        // measurable difference. Exact ties stay deterministic.
        if (effective > bestScore ||
            (effective == bestScore && static_cast<uint8_t>(route.transport) < static_cast<uint8_t>(best))) {
            bestScore = effective;
            best = route.transport;
        }
    }

    return best;
}

XRTeamTransport XRTransportTeam::preferredTransport(uint32_t destination, uint32_t nowMs)
{
    lock();
    const XRTeamTransport result = selectBestUnlocked(destination, nowMs, nullptr);
    unlock();
    return result;
}

bool XRTransportTeam::allowAssist(XRTeamTransport transport, uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    if (transport == XRTeamTransport::None || destination == 0 || packetId == 0)
        return false;

    lock();
    delivery_.expire(nowMs);
    (void)delivery_.begin(destination, packetId, nowMs);
    PacketState *packet = allocatePacket(destination, packetId, nowMs);
    if (!packet) {
        unlock();
        return false;
    }

    if (deadlinePending(nowMs, packet->assistUntilMs)) {
        unlock();
        return false;
    }

    packet->assistOwner = XRTeamTransport::None;
    packet->assistUntilMs = 0;

    const XRTeamTransport preferred = selectBestUnlocked(destination, nowMs, packet);
    if (preferred != transport) {
        unlock();
        return false;
    }

    if (!delivery_.startSecondary(destination, packetId, deliveryPathFor(transport), nowMs)) {
        unlock();
        return false;
    }

    packet->assistOwner = transport;
    packet->assistUntilMs = nowMs + ASSIST_RESERVATION_MS;
    packet->lastTouchedMs = nowMs;
    unlock();
    return true;
}

void XRTransportTeam::reportAssistResult(XRTeamTransport transport, uint32_t destination, uint32_t packetId, bool accepted,
                                         uint32_t nowMs)
{
    lock();
    PacketState *packet = findPacket(destination, packetId);
    if (packet && packet->assistOwner == transport) {
        packet->lastTouchedMs = nowMs;
        (void)delivery_.markCarrierResult(destination, packetId, deliveryPathFor(transport), accepted, nowMs);
        updateQualityUnlocked(destination, transport, accepted ? 56 : 18, nowMs);
        if (!accepted) {
            packet->assistOwner = XRTeamTransport::None;
            packet->assistUntilMs = 0;
            cooldownFor(*packet, transport) = nowMs + FAILED_TRANSPORT_COOLDOWN_MS;
        }
    }
    unlock();
}

void XRTransportTeam::notePrimaryFailed(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    if (destination == 0 || packetId == 0)
        return;

    lock();
    delivery_.expire(nowMs);
    (void)delivery_.markPrimaryFailed(destination, packetId, nowMs);
    (void)allocatePacket(destination, packetId, nowMs);
    unlock();
}

bool XRTransportTeam::claimRecovery(XRTeamTransport transport, uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    if (transport == XRTeamTransport::None || destination == 0 || packetId == 0)
        return false;

    lock();
    PacketState *packet = allocatePacket(destination, packetId, nowMs);
    if (!packet) {
        unlock();
        return false;
    }

    if (deadlinePending(nowMs, packet->ackWaitUntilMs)) {
        unlock();
        return false;
    }

    // Give all healthy sidecars a tiny window to publish their current route
    // score before choosing a recovery owner. Without this, scheduler order
    // could make the first task win even when the other radio has a better path.
    if (!packet->recoveryArbitrated) {
        if (packet->recoveryArbitrationUntilMs == 0)
            packet->recoveryArbitrationUntilMs = nowMs + RECOVERY_ARBITRATION_MS;
        if (deadlinePending(nowMs, packet->recoveryArbitrationUntilMs)) {
            unlock();
            return false;
        }
        packet->recoveryArbitrated = true;
    }

    if (packet->ackWaitUntilMs != 0) {
        // The previous carrier transfer completed but no end-to-end ACK arrived.
        // Give the other transport a short first chance before retrying the same
        // path, creating diversity without transmitting both at once.
        if (packet->lastAccepted != XRTeamTransport::None) {
            cooldownFor(*packet, packet->lastAccepted) = nowMs + POST_ACCEPT_SWITCH_MS;
            if (packet->lastAcceptedWasRecovery)
                updateQualityUnlocked(destination, packet->lastAccepted, 15, nowMs);
        }
        packet->lastAccepted = XRTeamTransport::None;
        packet->lastAcceptedWasRecovery = false;
        packet->ackWaitUntilMs = 0;
        (void)delivery_.queueRecovery(destination, packetId, nowMs);
    }

    if (deadlinePending(nowMs, packet->recoveryLeaseUntilMs)) {
        unlock();
        return false;
    }

    packet->recoveryOwner = XRTeamTransport::None;
    packet->recoveryLeaseUntilMs = 0;

    const XRTeamTransport preferred = selectBestUnlocked(destination, nowMs, packet);
    if (preferred != transport) {
        unlock();
        return false;
    }

    if (!delivery_.startSecondary(destination, packetId, deliveryPathFor(transport), nowMs)) {
        unlock();
        return false;
    }

    packet->recoveryOwner = transport;
    packet->recoveryLeaseUntilMs = nowMs + RECOVERY_LEASE_MS;
    packet->lastTouchedMs = nowMs;
    unlock();
    return true;
}

void XRTransportTeam::reportRecoveryResult(XRTeamTransport transport, uint32_t destination, uint32_t packetId,
                                           bool accepted, uint32_t nowMs)
{
    lock();
    PacketState *packet = findPacket(destination, packetId);
    if (!packet) {
        unlock();
        return;
    }

    if (packet->recoveryOwner == transport) {
        packet->recoveryOwner = XRTeamTransport::None;
        packet->recoveryLeaseUntilMs = 0;
    }

    packet->lastTouchedMs = nowMs;
    (void)delivery_.markCarrierResult(destination, packetId, deliveryPathFor(transport), accepted, nowMs);
    updateQualityUnlocked(destination, transport, accepted ? 62 : 10, nowMs);
    if (accepted) {
        packet->lastAccepted = transport;
        packet->lastAcceptedWasRecovery = true;
        packet->ackWaitUntilMs = nowMs + ACK_WAIT_MS;
    } else {
        cooldownFor(*packet, transport) = nowMs + FAILED_TRANSPORT_COOLDOWN_MS;
    }
    unlock();
}

void XRTransportTeam::markDelivered(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    lock();
    (void)delivery_.markDelivered(destination, packetId, nowMs);
    if (PacketState *packet = findPacket(destination, packetId)) {
        if (packet->lastAcceptedWasRecovery && packet->lastAccepted != XRTeamTransport::None) {
            updateQualityUnlocked(destination, packet->lastAccepted, 100, nowMs);
            if (DestinationMemory *memory = findDestination(destination)) {
                memory->preferred = packet->lastAccepted;
                memory->preferredUntilMs = nowMs + PREFERRED_PATH_HOLD_MS;
            }
        }
        *packet = {};
    }
    unlock();
}

void XRTransportTeam::markCancelled(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    lock();
    (void)delivery_.markCancelled(destination, packetId, nowMs);
    if (PacketState *packet = findPacket(destination, packetId)) {
        if (packet->lastAcceptedWasRecovery && packet->lastAccepted != XRTeamTransport::None)
            updateQualityUnlocked(destination, packet->lastAccepted, 5, nowMs);
        *packet = {};
    }
    unlock();
}

XRTransportTeam::LearningSnapshot XRTransportTeam::learningSnapshot()
{
    lock();
    LearningSnapshot snapshot{};
    for (size_t i = 0; i < destinations_.size(); ++i) {
        const auto &memory = destinations_[i];
        auto &record = snapshot.records[i];
        record.used = memory.used;
        record.destination = memory.destination;
        record.espNowQuality = memory.espNowQuality;
        record.xbeeQuality = memory.xbeeQuality;
        record.espNowSamples = memory.espNowSamples;
        record.xbeeSamples = memory.xbeeSamples;
    }
    snapshot.checksum = checksumLearningSnapshot(snapshot);
    unlock();
    return snapshot;
}

bool XRTransportTeam::restoreLearning(const LearningSnapshot &snapshot, uint32_t nowMs)
{
    if (snapshot.magic != 0x5852544c || snapshot.version != 1 ||
        snapshot.recordCount != MAX_DESTINATIONS ||
        snapshot.checksum != checksumLearningSnapshot(snapshot))
        return false;

    lock();
    destinations_ = {};
    for (size_t i = 0; i < destinations_.size(); ++i) {
        const auto &record = snapshot.records[i];
        if (!record.used || record.destination == 0)
            continue;

        auto &memory = destinations_[i];
        memory.used = true;
        memory.destination = record.destination;
        memory.espNowQuality = static_cast<int16_t>(clampScore(record.espNowQuality, 0, 100));
        memory.xbeeQuality = static_cast<int16_t>(clampScore(record.xbeeQuality, 0, 100));
        memory.espNowSamples = record.espNowSamples;
        memory.xbeeSamples = record.xbeeSamples;
        memory.preferred = XRTeamTransport::None;
        memory.preferredUntilMs = 0;
        memory.lastTouchedMs = nowMs;
    }
    learningDirty_ = false;
    unlock();
    return true;
}

bool XRTransportTeam::learningDirty()
{
    lock();
    const bool dirty = learningDirty_;
    unlock();
    return dirty;
}

void XRTransportTeam::markLearningPersisted()
{
    lock();
    learningDirty_ = false;
    unlock();
}

void XRTransportTeam::reset()
{
    lock();
    routes_ = {};
    packets_ = {};
    destinations_ = {};
    delivery_.reset();
    environment_ = {};
    learningDirty_ = false;
    environment_.batteryPercent = 100;
    environment_.noiseFloorDbm = -120;
    unlock();
}

} // namespace meshoffgrid::xr
