#include "MeshOffGridRouteBrain.h"

#include <algorithm>
#include <cstdint>

namespace meshoffgrid
{
namespace
{

struct ScoreWeights {
    uint8_t reliability;
    uint8_t latency;
    uint8_t throughput;
    uint8_t energy;
    uint8_t airtime;
    uint8_t confidence;
};

constexpr ScoreWeights weightsFor(DeliveryIntent intent)
{
    switch (intent) {
    case DeliveryIntent::Reliable:
        return {4, 1, 1, 1, 1, 3};
    case DeliveryIntent::Fast:
        return {2, 4, 4, 1, 1, 2};
    case DeliveryIntent::LowPower:
        return {2, 1, 1, 5, 3, 2};
    case DeliveryIntent::Critical:
        return {5, 1, 1, 1, 1, 4};
    case DeliveryIntent::Bulk:
        return {2, 2, 5, 1, 2, 2};
    }
    return {4, 1, 1, 1, 1, 3};
}

constexpr uint8_t bounded(uint8_t value)
{
    return value > 100 ? 100 : value;
}

bool winsTie(TransportKind candidate, TransportKind incumbent)
{
    return static_cast<uint8_t>(candidate) < static_cast<uint8_t>(incumbent);
}

} // namespace

uint16_t RouteBrain::score(const RouteCandidate &candidate, DeliveryIntent intent, uint32_t payloadBytes)
{
    if (!candidate.metrics.available)
        return 0;

    const auto weights = weightsFor(intent);
    int32_t total = 0;
    total += bounded(candidate.metrics.reliability) * weights.reliability;
    total += bounded(candidate.metrics.latencyQuality) * weights.latency;
    total += bounded(candidate.metrics.throughput) * weights.throughput;
    total += bounded(candidate.metrics.energyEfficiency) * weights.energy;
    total += bounded(candidate.metrics.airtimeEfficiency) * weights.airtime;
    total += bounded(candidate.metrics.confidence) * weights.confidence;

    const bool wantsBulk = intent == DeliveryIntent::Bulk || payloadBytes >= 1024;
    if (wantsBulk)
        total += candidate.metrics.bulkCapable ? 250 : -250;

    return static_cast<uint16_t>(std::max<int32_t>(0, total));
}

RouteDecision RouteBrain::select(const RouteCandidate *candidates, size_t count, DeliveryIntent intent,
                                 uint32_t payloadBytes, bool hasCurrent, TransportKind current, uint16_t switchMargin)
{
    RouteDecision decision;
    if (!candidates || count == 0)
        return decision;

    size_t bestIndex = count;
    size_t currentIndex = count;
    uint16_t bestScore = 0;
    uint16_t currentScore = 0;

    for (size_t i = 0; i < count; ++i) {
        if (!candidates[i].metrics.available)
            continue;

        const uint16_t candidateScore = score(candidates[i], intent, payloadBytes);
        if (hasCurrent && candidates[i].kind == current) {
            currentIndex = i;
            currentScore = candidateScore;
        }

        if (bestIndex == count || candidateScore > bestScore ||
            (candidateScore == bestScore && winsTie(candidates[i].kind, candidates[bestIndex].kind))) {
            bestIndex = i;
            bestScore = candidateScore;
        }
    }

    if (bestIndex == count)
        return decision;

    size_t primaryIndex = bestIndex;
    if (currentIndex != count && bestIndex != currentIndex &&
        static_cast<uint32_t>(bestScore) < static_cast<uint32_t>(currentScore) + switchMargin) {
        primaryIndex = currentIndex;
    }

    decision.hasPrimary = true;
    decision.primary = candidates[primaryIndex].kind;
    decision.primaryScore = score(candidates[primaryIndex], intent, payloadBytes);

    size_t backupIndex = count;
    uint16_t backupScore = 0;
    for (size_t i = 0; i < count; ++i) {
        if (i == primaryIndex || !candidates[i].metrics.available)
            continue;

        const uint16_t candidateScore = score(candidates[i], intent, payloadBytes);
        if (backupIndex == count || candidateScore > backupScore ||
            (candidateScore == backupScore && winsTie(candidates[i].kind, candidates[backupIndex].kind))) {
            backupIndex = i;
            backupScore = candidateScore;
        }
    }

    if (backupIndex != count) {
        decision.hasBackup = true;
        decision.backup = candidates[backupIndex].kind;
        decision.backupScore = backupScore;
    }

    return decision;
}

} // namespace meshoffgrid
