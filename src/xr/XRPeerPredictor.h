#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xr {

struct XRPeerObservation {
    uint32_t peerKey = 0; // local opaque peer identifier; no name/location stored
    uint8_t hourBucket = 0; // coarse 0..5 bucket, not exact time history
    int16_t rssiDbm = -127;
    int8_t snrDb = -20;
    uint8_t channelHealth = 50;
    bool packetReceived = false;
    bool ackReceived = false;
    bool directPath = false;
    bool relayedPath = false;
};

struct XRPeerPrediction {
    uint8_t directDeliveryProbability = 0;
    uint8_t ackReturnProbability = 0;
    uint8_t predictedLinkQuality = 0;
    uint8_t confidence = 0;
    bool asymmetricLinkLikely = false;
    bool shortWaitMayHelp = false;
};

struct XRPeerLearningCell {
    uint16_t observations = 0;
    uint16_t receives = 0;
    uint16_t acks = 0;
    uint16_t directReceives = 0;
    int16_t rssiEwma = -127;
    int16_t snrEwmaQ4 = -80; // dB * 4
    uint8_t channelHealthEwma = 50;
    uint32_t lastUpdateMs = 0;
};

struct XRPeerLearningRecord {
    uint32_t peerKey = 0;
    bool used = false;
    std::array<XRPeerLearningCell, 6> timeCells{};
};

class XRPeerPredictor {
  public:
    static constexpr size_t MAX_PEERS = 24;

    void observe(const XRPeerObservation &observation, uint32_t nowMs);
    XRPeerPrediction predict(uint32_t peerKey, uint8_t hourBucket, uint8_t currentChannelHealth, uint32_t nowMs) const;

    const XRPeerLearningRecord *record(uint32_t peerKey) const;

  private:
    std::array<XRPeerLearningRecord, MAX_PEERS> peers_{};

    XRPeerLearningRecord *findOrAllocate(uint32_t peerKey);
    const XRPeerLearningRecord *find(uint32_t peerKey) const;
    static uint16_t satInc(uint16_t value);
    static int16_t ewmaSigned(int16_t oldValue, int16_t sample, int16_t unsetFloor);
    static uint8_t ewma8(uint8_t oldValue, uint8_t sample);
};

} // namespace meshoffgrid::xr
