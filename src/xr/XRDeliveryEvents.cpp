#include "XRDeliveryEvents.h"

namespace meshoffgrid::xr {

std::array<XRDeliveryEventSink *, XRDeliveryEvents::MAX_SINKS> XRDeliveryEvents::sinks_{};

bool XRDeliveryEvents::addSink(XRDeliveryEventSink *sink)
{
    if (sink == nullptr)
        return false;

    for (auto *existing : sinks_) {
        if (existing == sink)
            return true;
    }

    for (auto &slot : sinks_) {
        if (slot == nullptr) {
            slot = sink;
            return true;
        }
    }
    return false;
}

void XRDeliveryEvents::removeSink(XRDeliveryEventSink *sink)
{
    for (auto &slot : sinks_) {
        if (slot == sink)
            slot = nullptr;
    }
}

void XRDeliveryEvents::notifyFailed(uint32_t destination, uint32_t packetId, uint32_t nowMs)
{
    for (auto *sink : sinks_) {
        if (sink != nullptr)
            sink->onReliableDeliveryFailed(destination, packetId, nowMs);
    }
}

void XRDeliveryEvents::notifyAcked(uint32_t peer, uint32_t packetId, uint32_t nowMs)
{
    for (auto *sink : sinks_) {
        if (sink != nullptr)
            sink->onReliableDeliveryAcked(peer, packetId, nowMs);
    }
}

void XRDeliveryEvents::notifyNaked(uint32_t peer, uint32_t packetId, uint32_t nowMs)
{
    for (auto *sink : sinks_) {
        if (sink != nullptr)
            sink->onReliableDeliveryNaked(peer, packetId, nowMs);
    }
}

} // namespace meshoffgrid::xr
