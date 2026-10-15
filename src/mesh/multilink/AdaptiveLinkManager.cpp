#include "AdaptiveLinkManager.h"

#include <algorithm>

namespace meshtastic::multilink {

namespace {
constexpr int32_t kRejected = -1000000;

int32_t clampSignal(int16_t rssiDbm)
{
    // Converts roughly -125..-45 dBm into 0..800. RSSI is deliberately only
    // one component because values are not perfectly comparable across PHYs.
    const int32_t shifted = static_cast<int32_t>(rssiDbm) + 125;
    return std::clamp<int32_t>(shifted * 10, 0, 800);
}

int32_t trafficBias(LinkType type, TrafficClass trafficClass)
{
    switch (trafficClass) {
    case TrafficClass::Control:
        if (type == LinkType::LoRa)
            return 220;
        if (type == LinkType::EspNow || type == LinkType::WifiNan)
            return 180;
        if (type == LinkType::XBee)
            return 210;
        return 0;
    case TrafficClass::Interactive:
        if (type == LinkType::EspNow || type == LinkType::WifiNan)
            return 260;
        if (type == LinkType::XBee)
            return 190;
        if (type == LinkType::Ble)
            return 140;
        return 40;
    case TrafficClass::Telemetry:
        if (type == LinkType::Backscatter)
            return 320;
        if (type == LinkType::LoRa)
            return 180;
        if (type == LinkType::XBee)
            return 150;
        return 40;
    case TrafficClass::Bulk:
        if (type == LinkType::WifiNan)
            return 320;
        if (type == LinkType::EspNow)
            return 220;
        if (type == LinkType::XBee)
            return 80;
        return -80;
    }
    return 0;
}
} // namespace

AdaptiveLinkManager::AdaptiveLinkManager(SelectionPolicy policy) : policy_(policy) {}

bool AdaptiveLinkManager::addLink(LinkTransport &link)
{
    if (linkCount_ >= links_.size())
        return false;

    for (size_t i = 0; i < linkCount_; ++i) {
        if (links_[i] == &link || links_[i]->type() == link.type())
            return false;
    }

    links_[linkCount_++] = &link;
    return true;
}

void AdaptiveLinkManager::poll(uint32_t nowMs)
{
    for (size_t i = 0; i < linkCount_; ++i)
        links_[i]->poll(nowMs);
}

size_t AdaptiveLinkManager::trafficIndex(TrafficClass trafficClass)
{
    return static_cast<size_t>(trafficClass);
}

LinkTransport *AdaptiveLinkManager::find(LinkType type) const
{
    for (size_t i = 0; i < linkCount_; ++i) {
        if (links_[i]->type() == type)
            return links_[i];
    }
    return nullptr;
}

int32_t AdaptiveLinkManager::score(const LinkTransport &link, const FrameView &frame, uint32_t nowMs,
                                   const SelectionPolicy &policy)
{
    if (!link.supports(frame))
        return kRejected;

    const LinkMetrics m = link.metrics();
    if (!m.available || !m.peerReachable || m.mtu < frame.size)
        return kRejected;
    if (frame.requireEncryption && !m.encrypted)
        return kRejected;
    if (m.deliveryPermille < policy.minDeliveryPermille)
        return kRejected;
    if (m.lastUpdateMs != 0 && (nowMs - m.lastUpdateMs) > policy.metricsMaxAgeMs)
        return kRejected;

    // Reliability dominates; latency, congestion, signal and energy refine it.
    int32_t result = static_cast<int32_t>(m.deliveryPermille) * 5;
    result += clampSignal(m.rssiDbm);
    result -= std::min<int32_t>(m.latencyMs, 2500);
    result -= std::min<int32_t>(m.queueDepth * 80, 1200);
    result -= std::min<int32_t>(m.energyCost, 1000);
    result += trafficBias(link.type(), frame.trafficClass);

    if (frame.requireAck)
        result += static_cast<int32_t>(m.deliveryPermille) / 2;

    return result;
}

SendPlan AdaptiveLinkManager::select(const FrameView &frame, uint32_t nowMs)
{
    LinkCandidate best;
    LinkCandidate second;

    for (size_t i = 0; i < linkCount_; ++i) {
        const int32_t candidateScore = score(*links_[i], frame, nowMs, policy_);
        if (candidateScore > best.score) {
            second = best;
            best = {links_[i], candidateScore};
        } else if (candidateScore > second.score) {
            second = {links_[i], candidateScore};
        }
    }

    SendPlan plan;
    if (!best.link)
        return plan;

    StickySelection &sticky = sticky_[trafficIndex(frame.trafficClass)];
    if (sticky.valid && (nowMs - sticky.selectedAtMs) < policy_.stickyWindowMs) {
        if (LinkTransport *previous = find(sticky.type)) {
            const int32_t previousScore = score(*previous, frame, nowMs, policy_);
            if (previousScore != kRejected && best.score < previousScore + policy_.hysteresisScore) {
                if (best.link != previous)
                    second = best;
                best = {previous, previousScore};
            }
        }
    }

    plan.links[0] = best.link;
    plan.count = 1;

    // Critical control traffic can use two independent bearers. The receiver
    // must deduplicate using (from,messageId).
    if (policy_.duplicateControlTraffic && frame.trafficClass == TrafficClass::Control && second.link &&
        second.link != best.link && second.score != kRejected) {
        plan.links[1] = second.link;
        plan.count = 2;
    }

    sticky = {best.link->type(), best.score, nowMs, true};
    return plan;
}

SendResult AdaptiveLinkManager::send(const FrameView &frame, uint32_t nowMs)
{
    const SendPlan plan = select(frame, nowMs);
    if (plan.count == 0)
        return SendResult::Unavailable;

    SendResult firstResult = SendResult::Unavailable;
    bool accepted = false;
    for (uint8_t i = 0; i < plan.count; ++i) {
        const SendResult result = plan.links[i]->send(frame);
        if (i == 0)
            firstResult = result;
        accepted = accepted || result == SendResult::Accepted;
    }

    return accepted ? SendResult::Accepted : firstResult;
}

bool AdaptiveLinkManager::seenRecently(uint32_t from, uint32_t messageId, uint32_t nowMs, uint32_t ttlMs)
{
    for (const auto &entry : dedupe_) {
        if (entry.valid && entry.from == from && entry.messageId == messageId && (nowMs - entry.seenAtMs) <= ttlMs)
            return true;
    }
    return false;
}

void AdaptiveLinkManager::remember(uint32_t from, uint32_t messageId, uint32_t nowMs)
{
    dedupe_[dedupeWrite_] = {from, messageId, nowMs, true};
    dedupeWrite_ = (dedupeWrite_ + 1) % dedupe_.size();
}

} // namespace meshtastic::multilink
