#include "EspNowLink.h"

namespace meshtastic::multilink {

EspNowLink::EspNowLink(EspNowBackend &backend, EspNowConfig config) : backend_(backend), config_(config)
{
    metrics_.mtu = config_.mtu;
    metrics_.encrypted = config_.upperLayerEncrypted;
    metrics_.deliveryPermille = 750; // optimistic but not trusted until callbacks arrive
    metrics_.latencyMs = 25;
    metrics_.energyCost = 260;
}

bool EspNowLink::begin()
{
    started_ = backend_.begin(config_);
    metrics_.available = started_ && backend_.available();
    return started_;
}

LinkMetrics EspNowLink::metrics() const
{
    LinkMetrics current = metrics_;
    current.available = started_ && backend_.available();
    current.peerReachable = current.available && lastPeer_ != 0 && backend_.hasPeer(lastPeer_);
    current.rssiDbm = (current.peerReachable) ? backend_.peerRssiDbm(lastPeer_) : -127;
    current.mtu = config_.mtu;
    current.encrypted = config_.upperLayerEncrypted;
    return current;
}

bool EspNowLink::supports(const FrameView &frame) const
{
    if (!started_ || frame.data == nullptr || frame.size == 0 || frame.size > config_.mtu)
        return false;
    if (config_.requireKnownPeer && !backend_.hasPeer(frame.to))
        return false;
    return true;
}

SendResult EspNowLink::send(const FrameView &frame)
{
    if (frame.size > config_.mtu)
        return SendResult::TooLarge;
    if (!supports(frame) || !backend_.available())
        return SendResult::Unavailable;

    lastPeer_ = frame.to;
    metrics_.peerReachable = backend_.hasPeer(frame.to);
    return backend_.send(frame.to, frame.data, frame.size) ? SendResult::Accepted : SendResult::Busy;
}

uint16_t EspNowLink::ewmaPermille(uint16_t oldValue, bool sample)
{
    const uint16_t target = sample ? 1000 : 0;
    return static_cast<uint16_t>((static_cast<uint32_t>(oldValue) * 7U + target) / 8U);
}

void EspNowLink::poll(uint32_t nowMs)
{
    if (!started_)
        return;

    backend_.poll(nowMs);
    metrics_.available = backend_.available();
    metrics_.lastUpdateMs = nowMs;

    EspNowDeliveryReport report;
    while (backend_.popDeliveryReport(report)) {
        metrics_.deliveryPermille = ewmaPermille(metrics_.deliveryPermille, report.success);
        if (report.latencyMs != 0)
            metrics_.latencyMs = report.latencyMs;
        if (report.nodeId != 0)
            lastPeer_ = report.nodeId;
    }

    if (lastPeer_ != 0) {
        metrics_.peerReachable = backend_.hasPeer(lastPeer_);
        if (metrics_.peerReachable)
            metrics_.rssiDbm = backend_.peerRssiDbm(lastPeer_);
    }
}

} // namespace meshtastic::multilink
