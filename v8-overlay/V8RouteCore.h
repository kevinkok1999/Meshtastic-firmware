#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ops {

enum class V8Route : uint8_t {
    LoRa = 0,
    EspNowLR = 1,
    XBeeXR868 = 2,
    LR2021 = 3,
    Count = 4
};

enum class V8RouteMode : uint8_t {
    AutoBalanced = 0,
    RangeFirst,
    PowerSave,
    LoRaOnly,
    EspNowOnly,
    XBeeOnly,
    LR2021Only,
    Redundant
};

struct V8LinkMetrics {
    bool available = false;
    bool reachable = false;
    uint16_t deliveryPermille = 700;
    uint16_t latencyMs = 1000;
    uint16_t queueDepth = 0;
    uint16_t energyCost = 500;
    uint32_t lastUpdateMs = 0;
    uint8_t failureStreak = 0;
};

class V8RouteCore {
public:
    V8RouteCore();

    void setMode(V8RouteMode mode) { _mode = mode; }
    V8RouteMode mode() const { return _mode; }

    void setAvailable(V8Route route, bool available, uint32_t nowMs);
    void setReachable(V8Route route, bool reachable, uint32_t nowMs);
    void noteSuccess(V8Route route, uint16_t latencyMs, uint32_t nowMs);
    void noteFailure(V8Route route, uint32_t nowMs);
    void setQueueDepth(V8Route route, uint16_t depth, uint32_t nowMs);

    V8Route choose(uint32_t nowMs);
    const V8LinkMetrics& metrics(V8Route route) const;

    static const char* name(V8Route route);

private:
    static constexpr uint32_t METRIC_MAX_AGE_MS = 30000;
    static constexpr uint32_t STICKY_WINDOW_MS = 2500;
    static constexpr int32_t HYSTERESIS_SCORE = 180;
    static constexpr int32_t REJECTED = -1000000;

    std::array<V8LinkMetrics, static_cast<size_t>(V8Route::Count)> _links{};
    V8RouteMode _mode = V8RouteMode::AutoBalanced;
    V8Route _lastRoute = V8Route::LoRa;
    int32_t _lastScore = REJECTED;
    uint32_t _lastSelectMs = 0;
    bool _haveLast = false;

    static size_t index(V8Route route) { return static_cast<size_t>(route); }
    bool allowed(V8Route route) const;
    int32_t score(V8Route route, uint32_t nowMs) const;
};

} // namespace ops
