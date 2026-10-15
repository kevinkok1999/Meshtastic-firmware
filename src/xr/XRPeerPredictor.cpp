#include "XRPeerPredictor.h"

#include <algorithm>
#include <climits>

namespace meshoffgrid::xr {

uint16_t XRPeerPredictor::satInc(uint16_t value)
{
    return value == UINT16_MAX ? value : static_cast<uint16_t>(value + 1u);
}

int16_t XRPeerPredictor::ewmaSigned(int16_t oldValue, int16_t sample, int16_t unsetFloor)
{
    if (oldValue <= unsetFloor)
        return sample;
    return static_cast<int16_t>((static_cast<int32_t>(oldValue) * 3 + sample) / 4);
}

uint8_t XRPeerPredictor::ewma8(uint8_t oldValue, uint8_t sample)
{
    return static_cast<uint8_t>((static_cast<uint16_t>(oldValue) * 3u + sample) / 4u);
}

const XRPeerLearningRecord *XRPeerPredictor::find(uint32_t peerKey) const
{
    for (const auto &peer : peers_) {
        if (peer.used && peer.peerKey == peerKey)
            return &peer;
    }
    return nullptr;
}

const XRPeerLearningRecord *XRPeerPredictor::record(uint32_t peerKey) const
{
    return find(peerKey);
}

XRPeerLearningRecord *XRPeerPredictor::findOrAllocate(uint32_t peerKey)
{
    for (auto &peer : peers_) {
        if (peer.used && peer.peerKey == peerKey)
            return &peer;
    }

    for (auto &peer : peers_) {
        if (!peer.used) {
            peer = XRPeerLearningRecord{};
            peer.used = true;
            peer.peerKey = peerKey;
            return &peer;
        }
    }

    auto *victim = &peers_[0];
    uint32_t oldest = UINT32_MAX;
    for (auto &peer : peers_) {
        uint32_t newest = 0;
        for (const auto &cell : peer.timeCells)
            newest = std::max(newest, cell.lastUpdateMs);
        if (newest < oldest) {
            oldest = newest;
            victim = &peer;
        }
    }

    *victim = XRPeerLearningRecord{};
    victim->used = true;
    victim->peerKey = peerKey;
    return victim;
}

void XRPeerPredictor::observe(const XRPeerObservation &observation, uint32_t nowMs)
{
    if (observation.peerKey == 0)
        return;

    auto *peer = findOrAllocate(observation.peerKey);
    auto &cell = peer->timeCells[observation.hourBucket % peer->timeCells.size()];

    cell.observations = satInc(cell.observations);
    if (observation.packetReceived)
        cell.receives = satInc(cell.receives);
    if (observation.ackReceived)
        cell.acks = satInc(cell.acks);
    if (observation.directPath && observation.packetReceived)
        cell.directReceives = satInc(cell.directReceives);

    if (observation.packetReceived) {
        cell.rssiEwma = ewmaSigned(cell.rssiEwma, observation.rssiDbm, -126);
        const int16_t snrQ4 = static_cast<int16_t>(observation.snrDb) * 4;
        cell.snrEwmaQ4 = ewmaSigned(cell.snrEwmaQ4, snrQ4, -79);
    }
    cell.channelHealthEwma = ewma8(cell.channelHealthEwma, observation.channelHealth);
    cell.lastUpdateMs = nowMs;
}

XRPeerPrediction XRPeerPredictor::predict(uint32_t peerKey, uint8_t hourBucket, uint8_t currentChannelHealth,
                                          uint32_t nowMs) const
{
    XRPeerPrediction out{};
    const auto *peer = find(peerKey);
    if (!peer)
        return out;

    const auto &cell = peer->timeCells[hourBucket % peer->timeCells.size()];
    if (cell.observations == 0)
        return out;

    const uint32_t ageMs = nowMs - cell.lastUpdateMs;
    int confidence = std::min<int>(100, static_cast<int>(cell.observations) * 10);
    if (ageMs > 24u * 60u * 60u * 1000u)
        confidence /= 2;
    if (ageMs > 7u * 24u * 60u * 60u * 1000u)
        confidence /= 2;

    const int receivePct = static_cast<int>(100u * cell.receives / cell.observations);
    const int ackPct = static_cast<int>(100u * cell.acks / cell.observations);
    const int directPct = static_cast<int>(100u * cell.directReceives / cell.observations);

    int quality = receivePct / 2 + directPct / 4 + std::min<int>(25, std::max<int>(0, (cell.rssiEwma + 120) * 2));
    quality += std::min<int>(15, std::max<int>(0, (cell.snrEwmaQ4 + 40) / 4));

    const int channelDelta = static_cast<int>(currentChannelHealth) - cell.channelHealthEwma;
    quality += channelDelta / 4;

    out.directDeliveryProbability = static_cast<uint8_t>(std::max(0, std::min(100, receivePct)));
    out.ackReturnProbability = static_cast<uint8_t>(std::max(0, std::min(100, ackPct)));
    out.predictedLinkQuality = static_cast<uint8_t>(std::max(0, std::min(100, quality)));
    out.confidence = static_cast<uint8_t>(confidence);

    // If packets commonly arrive but ACKs often fail, the reverse direction is
    // probably weaker. This is advisory; it never changes the wire protocol.
    out.asymmetricLinkLikely = cell.observations >= 4 && receivePct >= 60 && ackPct + 25 < receivePct;

    // A short wait is useful when history says this peer is usually reachable
    // in this coarse time bucket but current channel health is temporarily worse.
    out.shortWaitMayHelp = out.confidence >= 40 && receivePct >= 50 &&
                           currentChannelHealth + 15 < cell.channelHealthEwma;

    return out;
}

} // namespace meshoffgrid::xr
