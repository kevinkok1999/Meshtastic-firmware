#pragma once

#include <cstddef>
#include <cstdint>

namespace meshtastic::multilink {

enum class LinkType : uint8_t {
    LoRa = 0,
    EspNow,
    Ble,
    WifiNan,
    Backscatter,
};

enum class TrafficClass : uint8_t {
    Control = 0,
    Interactive,
    Telemetry,
    Bulk,
};

enum class SendResult : uint8_t {
    Accepted = 0,
    Busy,
    Unavailable,
    TooLarge,
    PolicyRejected,
    Error,
};

struct FrameView {
    const uint8_t *data = nullptr;
    size_t size = 0;
    uint32_t messageId = 0;
    uint32_t from = 0;
    uint32_t to = 0;
    TrafficClass trafficClass = TrafficClass::Interactive;
    bool requireAck = false;
    bool requireEncryption = true;
};

struct LinkMetrics {
    bool available = false;
    bool peerReachable = false;
    bool encrypted = false;
    int16_t rssiDbm = -127;
    int8_t snrDb = -30;
    uint16_t deliveryPermille = 0; // EWMA delivery probability, 0..1000
    uint16_t latencyMs = 0;
    uint16_t queueDepth = 0;
    uint16_t energyCost = 500;     // relative cost, lower is better, 0..1000
    uint16_t mtu = 0;
    uint32_t lastUpdateMs = 0;
};

class LinkTransport
{
  public:
    virtual ~LinkTransport() = default;

    virtual LinkType type() const = 0;
    virtual const char *name() const = 0;
    virtual LinkMetrics metrics() const = 0;
    virtual bool supports(const FrameView &frame) const = 0;
    virtual SendResult send(const FrameView &frame) = 0;
    virtual void poll(uint32_t nowMs) = 0;
};

} // namespace meshtastic::multilink
