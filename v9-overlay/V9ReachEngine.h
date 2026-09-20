#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ops {

enum class V9Route : uint8_t {
    LoRa = 0,
    EspNowLR = 1,
    XBeeXR868 = 2,
    LR2021 = 3,
    Count = 4
};

enum class V9Mode : uint8_t {
    Auto = 0,
    RangeFirst,
    PowerSave,
    LowLatency,
    Redundant,
    LoRaOnly,
    EspNowOnly,
    XBeeOnly,
    LR2021Only
};

enum class V9LinkClass : uint8_t {
    Unknown = 0,
    Poor,
    Marginal,
    Good,
    Strong
};

struct V9LinkMetric {
    bool available = false;
    bool reachable = false;
    uint16_t deliveryPermille = 700;
    uint16_t lossPermille = 300;
    uint16_t latencyMs = 1000;
    uint16_t retryPermille = 0;
    uint16_t airtimeCost = 500;
    uint16_t energyCost = 500;
    uint16_t queueDepth = 0;
    uint8_t hopCount = 0;
    uint8_t failureStreak = 0;
    V9LinkClass linkClass = V9LinkClass::Unknown;
    uint32_t lastUpdateMs = 0;
};

struct V9ReachDecision {
    V9Route primary = V9Route::LoRa;
    V9Route backup = V9Route::LoRa;
    bool hasPrimary = false;
    bool hasBackup = false;
    bool useFec = false;
    bool redundant = false;
    bool allowStoreForward = true;
    uint8_t retryRounds = 2;
    uint16_t fragmentBytes = 96;
    int32_t primaryScore = -1000000;
    int32_t backupScore = -1000000;
};

class V9ReachEngine {
public:
    V9ReachEngine();

    void setMode(V9Mode mode) { _mode = mode; }
    V9Mode mode() const { return _mode; }

    void setAvailable(V9Route route, bool available, uint32_t nowMs);
    void setReachable(V9Route route, bool reachable, uint32_t nowMs);
    void setLinkClass(V9Route route, V9LinkClass linkClass, uint32_t nowMs);
    void setQueueDepth(V9Route route, uint16_t depth, uint32_t nowMs);
    void setHopCount(V9Route route, uint8_t hops, uint32_t nowMs);

    void noteSuccess(V9Route route, uint16_t latencyMs, uint32_t nowMs);
    void noteFailure(V9Route route, uint32_t nowMs);
    void noteRetry(V9Route route, uint32_t nowMs);

    V9ReachDecision decide(uint32_t nowMs);
    const V9LinkMetric& metrics(V9Route route) const;

    static const char* name(V9Route route);

private:
    static constexpr int32_t REJECTED = -1000000;
    static constexpr uint32_t METRIC_MAX_AGE_MS = 45000;
    static constexpr uint32_t HYSTERESIS_WINDOW_MS = 3500;
    static constexpr int32_t HYSTERESIS_MARGIN = 220;

    std::array<V9LinkMetric, static_cast<size_t>(V9Route::Count)> _links{};
    V9Mode _mode = V9Mode::Auto;
    V9Route _lastPrimary = V9Route::LoRa;
    uint32_t _lastDecisionMs = 0;
    bool _haveLast = false;

    static size_t index(V9Route route) { return static_cast<size_t>(route); }
    bool allowed(V9Route route) const;
    int32_t score(V9Route route, uint32_t nowMs) const;
    static uint16_t classBonus(V9LinkClass linkClass);
};

} // namespace ops
