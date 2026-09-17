#pragma once

#include "LinkTransport.h"
#include <cstddef>
#include <cstdint>

namespace meshtastic::multilink {

enum class BackscatterMode : uint8_t {
    Disabled = 0,
    ExternalTag,
    ReaderAssisted,
    Ambient,
};

struct BackscatterConfig {
    BackscatterMode mode = BackscatterMode::Disabled;
    uint16_t mtu = 32;
    uint16_t symbolRate = 1000;
    bool upperLayerEncrypted = true;
};

/**
 * Hardware abstraction for a backscatter RF front-end.
 *
 * Stock T-Deck Plus hardware does not expose a controllable RF load switch on
 * the SX1262 antenna path. Concrete implementations therefore target an
 * external RF switch/tag/front-end. Keeping that dependency here prevents the
 * routing layer from pretending that backscatter can be enabled by software
 * alone.
 */
class BackscatterFrontEnd
{
  public:
    virtual ~BackscatterFrontEnd() = default;
    virtual bool begin(const BackscatterConfig &config) = 0;
    virtual bool available() const = 0;
    virtual bool carrierPresent() const = 0;
    virtual int16_t estimatedRssiDbm() const = 0;
    virtual bool modulate(const uint8_t *data, size_t size, uint16_t symbolRate) = 0;
    virtual void poll(uint32_t nowMs) = 0;
};

class BackscatterLink final : public LinkTransport
{
  public:
    BackscatterLink(BackscatterFrontEnd &frontEnd, BackscatterConfig config = {});

    bool begin();
    LinkType type() const override { return LinkType::Backscatter; }
    const char *name() const override { return "backscatter"; }
    LinkMetrics metrics() const override;
    bool supports(const FrameView &frame) const override;
    SendResult send(const FrameView &frame) override;
    void poll(uint32_t nowMs) override;

    void reportDelivery(bool success, uint16_t latencyMs = 0);

  private:
    BackscatterFrontEnd &frontEnd_;
    BackscatterConfig config_;
    LinkMetrics metrics_{};
    bool started_ = false;

    static uint16_t ewmaPermille(uint16_t oldValue, bool sample);
};

} // namespace meshtastic::multilink
