#pragma once

#include <stdint.h>

namespace meshoffgrid {

/**
 * Conservative RF decision helper for MeshOffGridNL V26.
 *
 * This deliberately does not change frequency, bandwidth, spreading factor or
 * coding rate. It only decides whether SX1262 RX boosted gain is useful based
 * on measurements already collected by the firmware.
 */
struct RFIntelligenceSnapshot {
    int32_t noiseFloorDbm = -120;
    float packetRssiDbm = -120.0f;
    float packetSnrDb = 0.0f;
    uint16_t cadBusyPermille = 0;
    uint16_t packetErrorPermille = 0;
    bool hasNoiseFloor = false;
    bool hasPacketMetrics = false;
    bool hasCadMetrics = false;
    bool hasErrorMetrics = false;
};

enum class RxGainDecision : uint8_t { KEEP = 0, POWER_SAVE, BOOSTED };

inline RxGainDecision chooseRxGain(const RFIntelligenceSnapshot &s, bool currentlyBoosted)
{
    if (!s.hasNoiseFloor)
        return RxGainDecision::KEEP;

    // Strong wideband/in-channel energy or a bad-packet storm can overload a
    // high-gain receive path. Prefer power-saving gain in that situation.
    const bool highNoise = s.noiseFloorDbm >= -102;
    const bool blockerPressure = s.hasCadMetrics && s.cadBusyPermille >= 800 && s.noiseFloorDbm >= -107;
    const bool decodePressure = s.hasErrorMetrics && s.packetErrorPermille >= 500;

    if (highNoise || blockerPressure || decodePressure)
        return RxGainDecision::POWER_SAVE;

    if (currentlyBoosted) {
        // Hysteresis: once boosted, do not drop out for small fluctuations.
        const bool moderateNoise = s.noiseFloorDbm >= -106;
        const bool risingErrors = s.hasErrorMetrics && s.packetErrorPermille >= 350;
        if (moderateNoise || risingErrors)
            return RxGainDecision::POWER_SAVE;
        return RxGainDecision::KEEP;
    }

    // Only enter boosted mode when the band is demonstrably quiet and packets
    // are weak. This avoids blindly amplifying interference.
    const bool cleanNoise = s.noiseFloorDbm <= -111;
    const bool cadAcceptable = !s.hasCadMetrics || s.cadBusyPermille <= 650;
    const bool weakUsefulSignal =
        s.hasPacketMetrics && (s.packetRssiDbm <= -105.0f || s.packetSnrDb <= 2.0f);
    const bool errorsAcceptable = !s.hasErrorMetrics || s.packetErrorPermille <= 250;

    if (cleanNoise && cadAcceptable && weakUsefulSignal && errorsAcceptable)
        return RxGainDecision::BOOSTED;

    return RxGainDecision::KEEP;
}

inline uint8_t rfQualityScore(const RFIntelligenceSnapshot &s)
{
    int score = 100;

    if (s.hasNoiseFloor) {
        if (s.noiseFloorDbm >= -100)
            score -= 40;
        else if (s.noiseFloorDbm >= -105)
            score -= 25;
        else if (s.noiseFloorDbm >= -110)
            score -= 10;
    }

    if (s.hasCadMetrics)
        score -= (s.cadBusyPermille * 25) / 1000;

    if (s.hasErrorMetrics)
        score -= (s.packetErrorPermille * 30) / 1000;

    if (s.hasPacketMetrics && s.packetSnrDb < 0.0f)
        score -= 10;

    if (score < 0)
        score = 0;
    if (score > 100)
        score = 100;
    return static_cast<uint8_t>(score);
}

} // namespace meshoffgrid
