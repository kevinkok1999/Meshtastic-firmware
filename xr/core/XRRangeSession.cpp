#include "XRRangeSession.h"

namespace meshoffgrid::xr {

XRRangeSession::XRRangeSession(const RangeSessionPolicy &policy) : policy_(policy) {}

bool XRRangeSession::elapsedMoreThan(uint32_t nowMs, uint32_t thenMs, uint32_t intervalMs)
{
    return static_cast<uint32_t>(nowMs - thenMs) > intervalMs;
}

void XRRangeSession::arm(uint32_t peerNode, uint32_t nowMs)
{
    peerNode_ = peerNode;
    state_ = RangeSessionState::ARMED;
    profile_ = RangeSessionProfile::STOCK_LONG_SLOW;
    metrics_ = {};
    startedMs_ = nowMs;
    lastActivityMs_ = nowMs;
    lastPeerSeenMs_ = 0;
    hasSeenPeer_ = false;
}

void XRRangeSession::activate(RangeSessionProfile profile, uint32_t nowMs)
{
    if (state_ == RangeSessionState::IDLE || peerNode_ == 0)
        return;
    profile_ = profile;
    state_ = RangeSessionState::ACTIVE;
    lastActivityMs_ = nowMs;
}

void XRRangeSession::stop()
{
    state_ = RangeSessionState::IDLE;
    profile_ = RangeSessionProfile::STOCK_LONG_SLOW;
    peerNode_ = 0;
    startedMs_ = 0;
    lastActivityMs_ = 0;
    lastPeerSeenMs_ = 0;
    hasSeenPeer_ = false;
}

void XRRangeSession::onDirectedTextStarted(uint32_t nowMs)
{
    if (state_ == RangeSessionState::IDLE)
        return;
    ++metrics_.txMessages;
    state_ = RangeSessionState::WAITING_FOR_ACK;
    lastActivityMs_ = nowMs;
}

void XRRangeSession::onDeliveryConfirmed(uint32_t nowMs)
{
    if (state_ == RangeSessionState::IDLE)
        return;
    ++metrics_.deliveredMessages;
    state_ = RangeSessionState::ACTIVE;
    lastActivityMs_ = nowMs;
}

void XRRangeSession::onFinalAckFailure(uint32_t nowMs)
{
    if (state_ == RangeSessionState::IDLE)
        return;
    ++metrics_.ackFailures;
    state_ = RangeSessionState::RECOVERY;
    lastActivityMs_ = nowMs;
}

void XRRangeSession::onPeerPacket(int16_t rssiDbm, float snrDb, float frequencyErrorHz, uint32_t nowMs)
{
    if (state_ == RangeSessionState::IDLE)
        return;
    ++metrics_.receivedPeerPackets;
    metrics_.lastRssiDbm = rssiDbm;
    metrics_.lastSnrDb = snrDb;
    if (policy_.collectFrequencyError)
        metrics_.frequencyErrorHz = frequencyErrorHz;
    lastPeerSeenMs_ = nowMs;
    hasSeenPeer_ = true;
    lastActivityMs_ = nowMs;
    if (state_ == RangeSessionState::RECOVERY)
        state_ = RangeSessionState::ACTIVE;
}

void XRRangeSession::updateChannel(int16_t noiseFloorDbm, uint8_t utilization)
{
    if (policy_.collectNoiseFloor)
        metrics_.noiseFloorDbm = noiseFloorDbm;
    metrics_.channelUtilization = utilization;
}

bool XRRangeSession::expired(uint32_t nowMs) const
{
    if (state_ == RangeSessionState::IDLE)
        return true;
    return elapsedMoreThan(nowMs, lastActivityMs_, policy_.maxSessionIdleMs);
}

bool XRRangeSession::peerFresh(uint32_t nowMs) const
{
    if (!hasSeenPeer_)
        return false;
    return !elapsedMoreThan(nowMs, lastPeerSeenMs_, policy_.peerFreshWindowMs);
}

bool XRRangeSession::shouldKeepReceiverHot(uint32_t nowMs) const
{
    if (!policy_.continuousRx || expired(nowMs))
        return false;
    return state_ == RangeSessionState::ACTIVE || state_ == RangeSessionState::WAITING_FOR_ACK ||
           state_ == RangeSessionState::RECOVERY;
}

bool XRRangeSession::shouldRequestRxBoost(uint32_t nowMs) const
{
    return policy_.rxBoostRequested && !expired(nowMs) && state_ != RangeSessionState::ARMED;
}

bool XRRangeSession::shouldFastReturnToRx(uint32_t nowMs) const
{
    return policy_.fastReturnToRxAfterTx && !expired(nowMs) &&
           (state_ == RangeSessionState::WAITING_FOR_ACK || state_ == RangeSessionState::RECOVERY ||
            state_ == RangeSessionState::ACTIVE);
}

bool XRRangeSession::allowsTraffic(RangeTrafficClass traffic, uint32_t nowMs) const
{
    if (state_ == RangeSessionState::IDLE || expired(nowMs))
        return true;
    switch (traffic) {
    case RangeTrafficClass::DIRECTED_TEXT:
    case RangeTrafficClass::ROUTING_ACK:
    case RangeTrafficClass::REQUIRED_MESH_CONTROL:
    case RangeTrafficClass::USER_LOCATION_SHARE:
        return true;
    case RangeTrafficClass::AUTOMATIC_POSITION:
        return !policy_.suppressAutomaticPosition;
    case RangeTrafficClass::NODE_INFO:
        return !policy_.suppressNonessentialDiscovery;
    case RangeTrafficClass::OPTIONAL_TELEMETRY:
        return !policy_.suppressOptionalTelemetry;
    case RangeTrafficClass::XR_BACKGROUND_SERVICE:
        return !policy_.suppressXrBackgroundServices;
    default:
        return true;
    }
}

} // namespace meshoffgrid::xr
