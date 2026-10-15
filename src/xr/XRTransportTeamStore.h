#pragma once

#include "XRTransportTeam.h"

#include <atomic>
#include <cstdint>

namespace meshoffgrid::xr {

// Power-loss-safe, wear-limited persistence for XRTransportTeam aggregate
// learning. It stores only fixed-size per-destination quality counters.
class XRTransportTeamStore
{
  public:
    static constexpr const char *PATH = "/prefs/xr_team.bin";
    static constexpr const char *TEMP_PATH = "/prefs/xr_team.tmp";
    static constexpr uint32_t MIN_PERSIST_INTERVAL_MS = 60u * 60u * 1000u;

    static XRTransportTeamStore &shared();

    // Safe to call from more than one XR sidecar. The first caller loads once;
    // later callers become no-ops.
    bool loadOnce(XRTransportTeam &team, uint32_t nowMs);

    // Saves only if learning changed, unless force=true. New learning that
    // arrives while the snapshot is being written stays dirty by generation.
    bool service(XRTransportTeam &team, uint32_t nowMs, bool force = false);

  private:
    std::atomic_flag operationLock_ = ATOMIC_FLAG_INIT;
    bool loaded_ = false;
    uint32_t lastPersistMs_ = 0;

    bool tryLock();
    void unlock();
};

} // namespace meshoffgrid::xr
