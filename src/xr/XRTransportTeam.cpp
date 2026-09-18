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
        // device thrash between transports.
        int effective = static_cast<int>(route.score);
        effective += (static_cast<int>(qualityForUnlocked(destination, route.transport)) - 50) / 5;

        if (route.transport == XRTeamTransport::EspNow)
            effective += 2; // lower-cost tie-break for a strong local direct path
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
            if (environment_.batteryPercent < 15)
                effective -= route.transport == XRTeamTransport::XBee ? 10 : 5;

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
        updateQualityUnlocked(destination, transport, accepted ? 56 : 18, nowMs);
        if (!accepted) {
            packet->assistOwner = XRTeamTransport::None;
            packet->assistUntilMs = 0;
            cooldownFor(*packet, transport) = nowMs + FAILED_TRANSPORT_COOLDOWN_MS;
        }
    }
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

void XRTransportTeam::markDelivered(uint32_t destination, uint32_t packetId)
{
    lock();
    if (PacketState *packet = findPacket(destination, packetId)) {
        if (packet->lastAcceptedWasRecovery && packet->lastAccepted != XRTeamTransport::None) {
            updateQualityUnlocked(destination, packet->lastAccepted, 100, packet->lastTouchedMs);
            if (DestinationMemory *memory = findDestination(destination)) {
                memory->preferred = packet->lastAccepted;
                memory->preferredUntilMs = packet->lastTouchedMs + PREFERRED_PATH_HOLD_MS;
            }
        }
        *packet = {};
    }
    unlock();
}

void XRTransportTeam::markCancelled(uint32_t destination, uint32_t packetId)
{
    lock();
    if (PacketState *packet = findPacket(destination, packetId)) {
        if (packet->lastAcceptedWasRecovery && packet->lastAccepted != XRTeamTransport::None)
            updateQualityUnlocked(destination, packet->lastAccepted, 5, packet->lastTouchedMs);
        *packet = {};
    }
    unlock();
}

void XRTransportTeam::reset()
{
    lock();
    routes_ = {};
    packets_ = {};
    destinations_ = {};
    environment_ = {};
    environment_.batteryPercent = 100;
    environment_.noiseFloorDbm = -120;
    unlock();
}

} // namespace meshoffgrid::xr
