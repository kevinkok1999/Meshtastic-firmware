#pragma once

#include "XRAdaptiveIntelligence.h"

namespace meshoffgrid::xr {

class XRAdaptiveStore {
  public:
    static constexpr const char *MODEL_PATH = "/prefs/xr_ai.bin";
    static constexpr const char *MODEL_TEMP_PATH = "/prefs/xr_ai.tmp";

    // Loads only aggregate learning state. No messages, SSIDs, peer names, or
    // precise locations are stored in this model.
    static bool load(XRAdaptiveIntelligence &engine);
    static bool loadAt(XRAdaptiveIntelligence &engine, const char *path, const char *tempPath = nullptr);

    // Persists a validated fixed-size snapshot. When protected storage is
    // active, the existing Meshtastic encrypted-storage path is used.
    static bool save(XRAdaptiveIntelligence &engine, uint32_t nowMs);
    static bool saveAt(XRAdaptiveIntelligence &engine, uint32_t nowMs, const char *path, const char *tempPath);
};

} // namespace meshoffgrid::xr
