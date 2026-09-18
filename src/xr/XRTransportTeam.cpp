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

void XRTransportTeam::reportRoute(XRTeamTransport transport, uint32_t destination, uint8_t score, bool available,
                                  uint32_t nowMs)
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

        // Tiny tie-break in favor of ESP-NOW because a good direct 2.4 GHz path
        // is generally lower-latency/lower-energy than waking a second 868 MHz
        // radio. A clearly stronger XBee score still wins.
        int effective = static_cast<int>(route.score);
        if (route.transport == XRTeamTransport::EspNow)
            effective += 2;

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
        if (packet->lastAccepted != XRTeamTransport::None)
            cooldownFor(*packet, packet->lastAccepted) = nowMs + POST_ACCEPT_SWITCH_MS;
        packet->lastAccepted = XRTeamTransport::None;
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
    if (accepted) {
        packet->lastAccepted = transport;
        packet->ackWaitUntilMs = nowMs + ACK_WAIT_MS;
    } else {
        cooldownFor(*packet, transport) = nowMs + FAILED_TRANSPORT_COOLDOWN_MS;
    }
    unlock();
}

void XRTransportTeam::markDelivered(uint32_t destination, uint32_t packetId)
{
    lock();
    if (PacketState *packet = findPacket(destination, packetId))
        *packet = {};
    unlock();
}

void XRTransportTeam::markCancelled(uint32_t destination, uint32_t packetId)
{
    markDelivered(destination, packetId);
}

void XRTransportTeam::reset()
{
    lock();
    routes_ = {};
    packets_ = {};
    unlock();
}

} // namespace meshoffgrid::xr
