#pragma once

#include <cstddef>
#include <cstdint>

namespace meshoffgrid
{

enum class TransportKind : uint8_t {
    LongLink = 0,
    DirectLink,
    FastLink,
    NetLink,
    NearLink,
    CarryLink,
    GateLink,
};

enum class DeliveryIntent : uint8_t {
    Reliable = 0,
    Fast,
    LowPower,
    Critical,
    Bulk,
};

struct LinkMetrics {
    bool available = false;
    bool bulkCapable = false;
    uint8_t reliability = 0;
    uint8_t latencyQuality = 0;
    uint8_t throughput = 0;
    uint8_t energyEfficiency = 0;
    uint8_t airtimeEfficiency = 0;
    uint8_t confidence = 0;
};

struct RouteCandidate {
    TransportKind kind = TransportKind::LongLink;
    LinkMetrics metrics{};
};

struct RouteDecision {
    bool hasPrimary = false;
    bool hasBackup = false;
    TransportKind primary = TransportKind::LongLink;
    TransportKind backup = TransportKind::LongLink;
    uint16_t primaryScore = 0;
    uint16_t backupScore = 0;
};

class RouteBrain
{
  public:
    static RouteDecision select(const RouteCandidate *candidates, size_t count, DeliveryIntent intent, uint32_t payloadBytes,
                                bool hasCurrent = false, TransportKind current = TransportKind::LongLink,
                                uint16_t switchMargin = 80);

  private:
    static uint16_t score(const RouteCandidate &candidate, DeliveryIntent intent, uint32_t payloadBytes);
};

} // namespace meshoffgrid
