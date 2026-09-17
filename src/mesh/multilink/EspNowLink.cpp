#include "EspNowLink.h"

namespace meshtastic::multilink {

EspNowLink::EspNowLink(EspNowBackend &backend, EspNowConfig config) : backend_(backend), config_(config)
{
    metrics_.mtu = config_.mtu;
    metrics_.encrypted = config_.upperLayerEncrypted;
    metrics_.deliveryPermille = 750; // bootstrap value until delivery callbacks arrive
    metrics_.latencyMs = 25;
    metrics_.energyCost = 260;
}

bool EspNowLink::begin()
{
    started_ = backend_.begin(config_);
    metrics_.available = started_ && backend_.available();
    metrics_.peerReachable = metrics_.available;
    return started_;
}

LinkMetrics EspNowLink::metrics() const
{
    LinkMetrics current = metrics_;
    current.available = started_ && backend_.available();

    // Per-destination reachability belongs in supports(frame). The global
    // metric must remain selectable before the first packet has been sent.
    current.peerReachable = current.available;
    current.rssiDbm = (current.available && lastPeer_ != 0 && backend_.hasPeer(lastPeer_))
                          ? backend_.peerRssiDbm(lastPeer_)
                          : -90;
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
    metrics_.peerReachable = metrics_.available;
    metrics_.lastUpdateMs = nowMs;

    EspNowDeliveryReport report;
    while (backend_.popDeliveryReport(report)) {
        metrics_.deliveryPermille = ewmaPermille(metrics_.deliveryPermille, report.success);
        if (report.latencyMs != 0)
            metrics_.latencyMs = report.latencyMs;
        if (report.nodeId != 0)
            lastPeer_ = report.nodeId;
    }

    EspNowRxFrame rx;
    while (backend_.popReceived(rx)) {
        if (rx.nodeId != 0)
            lastPeer_ = rx.nodeId;

        if (receiveSink_ != nullptr && rx.size != 0) {
            ReceivedFrameView view;
            view.data = rx.data.data();
            view.size = rx.size;
            view.link = LinkType::EspNow;
            view.from = rx.nodeId;
            view.rssiDbm = rx.rssiDbm;
            view.transportAuthenticated = rx.transportAuthenticated;
            receiveSink_->onLinkFrame(view);
        }
    }

    if (lastPeer_ != 0 && backend_.hasPeer(lastPeer_))
        metrics_.rssiDbm = backend_.peerRssiDbm(lastPeer_);
}

} // namespace meshtastic::multilink
