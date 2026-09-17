#pragma once

#include <cstdint>

namespace meshoffgrid::xr {

// Simple local RF coexistence guard for two 868 MHz radios in the same handheld.
// It does not alter either radio's regulatory settings; it only prevents the
// secondary radio from transmitting while LoRa is active or inside a guard window.
class XRRfCoexistence
{
  public:
    void onLoRaTxStart(uint32_t nowMs)
    {
        loraTxActive_ = true;
        lastLoRaActivityMs_ = nowMs;
    }

    void onLoRaTxEnd(uint32_t nowMs, bool ackExpected)
    {
        loraTxActive_ = false;
        lastLoRaActivityMs_ = nowMs;
        const uint32_t guard = ackExpected ? ACK_GUARD_MS : NORMAL_GUARD_MS;
        secondaryBlockedUntilMs_ = nowMs + guard;
    }

    bool canUseSecondary(uint32_t nowMs) const
    {
        return !loraTxActive_ && static_cast<int32_t>(nowMs - secondaryBlockedUntilMs_) >= 0;
    }

    uint32_t remainingBlockMs(uint32_t nowMs) const
    {
        if (canUseSecondary(nowMs))
            return 0;
        if (loraTxActive_)
            return NORMAL_GUARD_MS;
        return secondaryBlockedUntilMs_ - nowMs;
    }

    bool loraTxActive() const { return loraTxActive_; }
    uint32_t lastLoRaActivityMs() const { return lastLoRaActivityMs_; }

  private:
    static constexpr uint32_t NORMAL_GUARD_MS = 350;
    static constexpr uint32_t ACK_GUARD_MS = 1400;

    bool loraTxActive_ = false;
    uint32_t lastLoRaActivityMs_ = 0;
    uint32_t secondaryBlockedUntilMs_ = 0;
};

} // namespace meshoffgrid::xr
