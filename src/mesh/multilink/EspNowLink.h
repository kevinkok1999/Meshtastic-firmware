#pragma once

#include "LinkTransport.h"
#include <cstddef>
#include <cstdint>

namespace meshtastic::multilink {

struct EspNowConfig {
    uint8_t channel = 0; // 0 = keep current Wi-Fi channel
    uint16_t mtu = 240;  // leave room for our own framing/forward compatibility
    bool upperLayerEncrypted = true;
    bool requireKnownPeer = true;
};

struct EspNowDeliveryReport {
    uint32_t nodeId = 0;
    bool success = false;
    uint16_t latencyMs = 0;
};

class EspNowBackend
{
  public:
    virtual ~EspNowBackend() = default;

    virtual bool begin(const EspNowConfig &config) = 0;
    virtual bool available() const = 0;
    virtual bool hasPeer(uint32_t nodeId) const = 0;
    virtual bool send(uint32_t nodeId, const uint8_t *data, size_t size) = 0;
    virtual int16_t peerRssiDbm(uint32_t nodeId) const = 0;
    virtual bool popDeliveryReport(EspNowDeliveryReport &report) = 0;
    virtual void poll(uint32_t nowMs) = 0;
};

class EspNowLink final : public LinkTransport
{
  public:
    explicit EspNowLink(EspNowBackend &backend, EspNowConfig config = {});

    bool begin();

    LinkType type() const override { return LinkType::EspNow; }
    const char *name() const override { return "esp-now"; }
    LinkMetrics metrics() const override;
    bool supports(const FrameView &frame) const override;
    SendResult send(const FrameView &frame) override;
    void poll(uint32_t nowMs) override;

    uint32_t lastPeer() const { return lastPeer_; }

  private:
    EspNowBackend &backend_;
    EspNowConfig config_;
    LinkMetrics metrics_{};
    uint32_t lastPeer_ = 0;
    bool started_ = false;

    static uint16_t ewmaPermille(uint16_t oldValue, bool sample);
};

} // namespace meshtastic::multilink
