#pragma once

#include <atomic>
#include <cstdint>

namespace meshoffgrid::xr {

// Thread-safe local RF coexistence guard for two 868 MHz radios in the same handheld.
// LoRa TX hooks and the secondary-radio task run on different execution contexts,
// so all shared state is atomic. This guard does not change RF parameters; it only
// schedules local activity to reduce self-interference/desense.
class XRRfCoexistence
{
  public:
    void onLoRaTxStart(uint32_t nowMs)
    {
        lastLoRaActivityMs_.store(nowMs, std::memory_order_relaxed);
        loraTxActive_.store(true, std::memory_order_release);
    }

    void onLoRaTxEnd(uint32_t nowMs, bool ackExpected)
    {
        lastLoRaActivityMs_.store(nowMs, std::memory_order_relaxed);
        const uint32_t guard = ackExpected ? ACK_GUARD_MS : NORMAL_GUARD_MS;
        secondaryBlockedUntilMs_.store(nowMs + guard, std::memory_order_relaxed);
        loraTxActive_.store(false, std::memory_order_release);
    }

    bool canUseSecondary(uint32_t nowMs) const
    {
        if (loraTxActive_.load(std::memory_order_acquire))
            return false;
        const uint32_t blockedUntil = secondaryBlockedUntilMs_.load(std::memory_order_relaxed);
        return static_cast<int32_t>(nowMs - blockedUntil) >= 0;
    }

    uint32_t remainingBlockMs(uint32_t nowMs) const
    {
        if (loraTxActive_.load(std::memory_order_acquire))
            return NORMAL_GUARD_MS;

        const uint32_t blockedUntil = secondaryBlockedUntilMs_.load(std::memory_order_relaxed);
        if (static_cast<int32_t>(nowMs - blockedUntil) >= 0)
            return 0;
        return blockedUntil - nowMs;
    }

    bool loraTxActive() const { return loraTxActive_.load(std::memory_order_acquire); }
    uint32_t lastLoRaActivityMs() const { return lastLoRaActivityMs_.load(std::memory_order_relaxed); }

  private:
    static constexpr uint32_t NORMAL_GUARD_MS = 350;
    static constexpr uint32_t ACK_GUARD_MS = 1400;

    std::atomic<bool> loraTxActive_{false};
    std::atomic<uint32_t> lastLoRaActivityMs_{0};
    std::atomic<uint32_t> secondaryBlockedUntilMs_{0};
};

} // namespace meshoffgrid::xr
