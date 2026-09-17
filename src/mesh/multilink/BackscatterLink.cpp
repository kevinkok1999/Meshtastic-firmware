#include "BackscatterLink.h"

namespace meshtastic::multilink {

BackscatterLink::BackscatterLink(BackscatterFrontEnd &frontEnd, BackscatterConfig config)
    : frontEnd_(frontEnd), config_(config)
{
    metrics_.mtu = config_.mtu;
    metrics_.encrypted = config_.upperLayerEncrypted;
    metrics_.energyCost = 40;       // relative hint; tune from measurements per front-end
    metrics_.deliveryPermille = 500; // neutral bootstrap value until measurements arrive
}

bool BackscatterLink::begin()
{
    if (config_.mode == BackscatterMode::Disabled)
        return false;

    started_ = frontEnd_.begin(config_);
    metrics_.available = started_ && frontEnd_.available();
    metrics_.peerReachable = metrics_.available && frontEnd_.carrierPresent();
    return started_;
}

LinkMetrics BackscatterLink::metrics() const
{
    LinkMetrics current = metrics_;
    current.available = started_ && frontEnd_.available();
    current.peerReachable = current.available && frontEnd_.carrierPresent();
    current.rssiDbm = frontEnd_.estimatedRssiDbm();
    current.mtu = config_.mtu;
    current.encrypted = config_.upperLayerEncrypted;
    return current;
}

bool BackscatterLink::supports(const FrameView &frame) const
{
    if (!started_ || config_.mode == BackscatterMode::Disabled)
        return false;
    if (frame.size == 0 || frame.data == nullptr || frame.size > config_.mtu)
        return false;

    // Start as a low-rate bearer. Bulk traffic is deliberately rejected until
    // a measured front-end proves that it can sustain it.
    return frame.trafficClass != TrafficClass::Bulk;
}

SendResult BackscatterLink::send(const FrameView &frame)
{
    if (!supports(frame))
        return frame.size > config_.mtu ? SendResult::TooLarge : SendResult::Unavailable;
    if (!frontEnd_.available() || !frontEnd_.carrierPresent())
        return SendResult::Unavailable;

    return frontEnd_.modulate(frame.data, frame.size, config_.symbolRate) ? SendResult::Accepted : SendResult::Busy;
}

void BackscatterLink::poll(uint32_t nowMs)
{
    if (!started_)
        return;

    frontEnd_.poll(nowMs);
    metrics_.available = frontEnd_.available();
    metrics_.peerReachable = metrics_.available && frontEnd_.carrierPresent();
    metrics_.rssiDbm = frontEnd_.estimatedRssiDbm();
    metrics_.lastUpdateMs = nowMs;
}

uint16_t BackscatterLink::ewmaPermille(uint16_t oldValue, bool sample)
{
    const uint16_t target = sample ? 1000 : 0;
    // alpha = 1/8, integer only: friendly to embedded targets.
    return static_cast<uint16_t>((static_cast<uint32_t>(oldValue) * 7U + target) / 8U);
}

void BackscatterLink::reportDelivery(bool success, uint16_t latencyMs)
{
    metrics_.deliveryPermille = ewmaPermille(metrics_.deliveryPermille, success);
    if (latencyMs != 0)
        metrics_.latencyMs = latencyMs;
}

} // namespace meshtastic::multilink
