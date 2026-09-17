#pragma once

#include <atomic>
#include <cstdint>

namespace meshoffgrid::xr {

// Thread-safe local RF coexistence guard for two sub-GHz radios in one handheld.
// It never changes RF power/frequency/regulatory settings. It only blocks the
// optional secondary transmitter while the primary LoRa TX path is active and
// for a conservative guard window afterwards.
class XRRfCoexistence
{
  public:
    void onLoRaTxStart(uint32_t nowMs)
    {
        lastLoRaActivityMs_.store(nowMs);
        loraTxActive_.store(true);
    }

    void onLoRaTxEnd(uint32_t nowMs, bool ackExpected)
    {
        const uint32_t guard = ackExpected ? ACK_GUARD_MS : NORMAL_GUARD_MS;
        lastLoRaActivityMs_.store(nowMs);
        secondaryBlockedUntilMs_.store(nowMs + guard);
        loraTxActive_.store(false);
    }

    bool canUseSecondary(uint32_t nowMs) const
    {
        if (loraTxActive_.load())
            return false;
        const uint32_t blockedUntil = secondaryBlockedUntilMs_.load();
        return static_cast<int32_t>(nowMs - blockedUntil) >= 0;
    }

    uint32_t remainingBlockMs(uint32_t nowMs) const
    {
        if (loraTxActive_.load())
            return NORMAL_GUARD_MS;

        const uint32_t blockedUntil = secondaryBlockedUntilMs_.load();
        if (static_cast<int32_t>(nowMs - blockedUntil) >= 0)
            return 0;
        return blockedUntil - nowMs;
    }

    bool loraTxActive() const { return loraTxActive_.load(); }
    uint32_t lastLoRaActivityMs() const { return lastLoRaActivityMs_.load(); }

  private:
    static constexpr uint32_t NORMAL_GUARD_MS = 350;
    static constexpr uint32_t ACK_GUARD_MS = 1400;

    std::atomic<bool> loraTxActive_{false};
    std::atomic<uint32_t> lastLoRaActivityMs_{0};
    std::atomic<uint32_t> secondaryBlockedUntilMs_{0};
};

} // namespace meshoffgrid::xr
