#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xr {

class XRDeliveryEventSink
{
  public:
    virtual ~XRDeliveryEventSink() = default;

    // The normal LoRa reliable-delivery budget has been exhausted for a local DM.
    // This is a trigger for alternative transports/store-and-forward, not proof
    // that the message can never be delivered.
    virtual void onReliableDeliveryFailed(uint32_t destination, uint32_t packetId, uint32_t nowMs)
    {
        (void)destination;
        (void)packetId;
        (void)nowMs;
    }

    // An end-to-end ACK for a locally-originated packet arrived through the
    // normal Meshtastic router. Alternative transports should cancel retries.
    virtual void onReliableDeliveryAcked(uint32_t peer, uint32_t packetId, uint32_t nowMs)
    {
        (void)peer;
        (void)packetId;
        (void)nowMs;
    }

    // A remote node explicitly NAKed a locally-originated packet. Local
    // MAX_RETRANSMIT notifications are not emitted as remote NAK events.
    virtual void onReliableDeliveryNaked(uint32_t peer, uint32_t packetId, uint32_t nowMs)
    {
        (void)peer;
        (void)packetId;
        (void)nowMs;
    }
};

class XRDeliveryEvents
{
  public:
    static constexpr size_t MAX_SINKS = 4;

    static bool addSink(XRDeliveryEventSink *sink);
    static void removeSink(XRDeliveryEventSink *sink);

    static void notifyFailed(uint32_t destination, uint32_t packetId, uint32_t nowMs);
    static void notifyAcked(uint32_t peer, uint32_t packetId, uint32_t nowMs);
    static void notifyNaked(uint32_t peer, uint32_t packetId, uint32_t nowMs);

  private:
    static std::array<XRDeliveryEventSink *, MAX_SINKS> sinks_;
};

} // namespace meshoffgrid::xr
