#include "XRSelfHealingMonitor.h"

#include <algorithm>
#include <set>

namespace meshoffgrid::xr {

void XRSelfHealingMonitor::observe(const XRHealthObservation &observation, uint32_t nowMs)
{
    samples_[writeIndex_].observation = observation;
    samples_[writeIndex_].timeMs = nowMs;
    samples_[writeIndex_].used = true;
    writeIndex_ = (writeIndex_ + 1u) % samples_.size();
}

XRSelfHealingDecision XRSelfHealingMonitor::diagnose(uint32_t nowMs) const
{
    XRSelfHealingDecision out{};

    uint16_t valid = 0;
    uint16_t received = 0;
    uint16_t acked = 0;
    uint16_t poorChannel = 0;
    uint16_t largeFreqError = 0;
    int32_t rssiSum = 0;
    int32_t snrSum = 0;
    std::set<uint32_t> peers;

    for (const auto &sample : samples_) {
        if (!sample.used)
            continue;
        if (nowMs - sample.timeMs > 5u * 60u * 1000u)
            continue;

        ++valid;
        if (sample.observation.peerKey)
            peers.insert(sample.observation.peerKey);
        if (sample.observation.packetReceived) {
            ++received;
            rssiSum += sample.observation.rssiDbm;
            snrSum += sample.observation.snrDb;
        }
        if (sample.observation.ackReceived)
            ++acked;
        if (sample.observation.channelHealth < 30)
            ++poorChannel;
        if (abs32(sample.observation.frequencyErrorHz) > 3500)
            ++largeFreqError;
    }

    if (valid < 4)
        return out;

    const int receivePct = static_cast<int>(100u * received / valid);
    const int ackPct = static_cast<int>(100u * acked / valid);
    const int poorChannelPct = static_cast<int>(100u * poorChannel / valid);
    const int freqErrorPct = static_cast<int>(100u * largeFreqError / valid);

    if (poorChannelPct >= 60) {
        out.fault = XRFaultClass::TEMPORARY_CONGESTION;
        out.confidence = static_cast<uint8_t>(std::min(100, 55 + poorChannelPct / 2));
        out.suppressOptionalBackground = true;
        out.recommendShortBackoff = true;
        return out;
    }

    if (freqErrorPct >= 60 && peers.size() >= 2) {
        out.fault = XRFaultClass::FREQUENCY_STABILITY_SUSPECT;
        out.confidence = static_cast<uint8_t>(std::min(100, 50 + freqErrorPct / 2));
        out.requestRadioReinit = true;
        out.requestRxFocus = true;
        return out;
    }

    if (received >= 4 && receivePct >= 60 && ackPct + 25 < receivePct) {
        out.fault = XRFaultClass::ASYMMETRIC_LINK;
        out.confidence = static_cast<uint8_t>(std::min(100, receivePct - ackPct + 45));
        out.requestRxFocus = true;
        out.preferAlternateTransport = true;
        return out;
    }

    if (peers.size() >= 3 && receivePct <= 35 && poorChannelPct < 35) {
        out.fault = XRFaultClass::BROAD_LINK_DEGRADATION;
        out.confidence = static_cast<uint8_t>(std::min(100, 70 + static_cast<int>(peers.size()) * 5));
        out.requestRadioReinit = true;
        out.requestRxFocus = true;
        return out;
    }

    // A single peer can disappear without implying a device-wide problem.
    if (peers.size() == 1 && receivePct <= 25 && poorChannelPct < 35) {
        out.fault = XRFaultClass::PEER_SPECIFIC_LOSS;
        out.confidence = 60;
        out.recommendShortBackoff = true;
        out.preferAlternateTransport = true;
        return out;
    }

    // Keep a tiny amount of aggregate math here so diagnostics can later show
    // trends without retaining raw logs. The values are intentionally not
    // exposed yet; this avoids adding UI/debug noise before integration.
    (void)rssiSum;
    (void)snrSum;
    return out;
}

} // namespace meshoffgrid::xr
