#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xr {

struct XREncounterRecord {
    uint32_t peerKey = 0;
    bool used = false;
    uint16_t encounters = 0;
    uint16_t directSuccesses = 0;
    uint16_t courierAccepts = 0;
    uint16_t courierReceipts = 0;
    uint32_t lastSeenMs = 0;
    uint32_t meanInterEncounterMs = 0;
};

struct XREncounterPrediction {
    uint8_t encounterProbability = 0;
    uint8_t directUtility = 0;
    uint8_t courierUtility = 0;
    uint8_t confidence = 0;
};

class XREncounterGraph {
  public:
    static constexpr size_t MAX_PEERS = 32;

    void observeEncounter(uint32_t peerKey, bool directSuccess, uint32_t nowMs);
    void recordCourierAccept(uint32_t peerKey, uint32_t nowMs);
    void recordCourierReceipt(uint32_t peerKey, uint32_t nowMs);

    XREncounterPrediction predict(uint32_t peerKey, uint32_t nowMs) const;
    int bestCourierCandidate(const uint32_t *peerKeys, size_t count, uint32_t nowMs, uint8_t minimumUtility = 55) const;

    const XREncounterRecord *record(uint32_t peerKey) const;

  private:
    std::array<XREncounterRecord, MAX_PEERS> records_{};

    XREncounterRecord *findOrAllocate(uint32_t peerKey);
    const XREncounterRecord *find(uint32_t peerKey) const;
    static uint16_t satInc(uint16_t value);
};

} // namespace meshoffgrid::xr
