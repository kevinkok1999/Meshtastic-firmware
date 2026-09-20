#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ops {
namespace v10 {

enum Bearer : uint8_t {
    BEARER_LORA = 0,
    BEARER_GFSK = 1,
    BEARER_ESPNOW_LR = 2,
    BEARER_WIFI_LR = 3,
    BEARER_BLE_CODED = 4,
    BEARER_COUNT = 5
};

enum Resource : uint8_t {
    RESOURCE_SUBGHZ = 0,
    RESOURCE_RF24 = 1
};

enum LinkClass : uint8_t {
    LINK_UNKNOWN = 0,
    LINK_POOR,
    LINK_MARGINAL,
    LINK_GOOD,
    LINK_STRONG
};

struct Metric {
    bool available;
    bool reachable;
    uint16_t deliveryPermille;
    uint16_t latencyMs;
    uint16_t retryPermille;
    uint16_t transitionCost;
    uint16_t energyCost;
    uint8_t failures;
    LinkClass linkClass;
    uint32_t updatedMs;

    Metric();
};

struct Decision {
    bool hasPrimary;
    bool hasBackup;
    Bearer primary;
    Bearer backup;
    int32_t primaryScore;
    int32_t backupScore;
    uint8_t retryRounds;
    bool useFec;
    uint16_t fragmentBytes;

    Decision();
};

class DirectLinkBrain {
public:
    DirectLinkBrain();

    void setAvailable(Bearer b, bool value, uint32_t nowMs);
    void setReachable(Bearer b, bool value, uint32_t nowMs);
    void setLinkClass(Bearer b, LinkClass value, uint32_t nowMs);
    void noteSuccess(Bearer b, uint16_t latencyMs, uint32_t nowMs);
    void noteFailure(Bearer b, uint32_t nowMs);
    void noteRetry(Bearer b, uint32_t nowMs);

    Decision decide(uint32_t nowMs);
    const Metric& metric(Bearer b) const;

    static Resource resource(Bearer b);
    static const char* name(Bearer b);

private:
    static const int32_t REJECTED = -1000000;
    static const uint32_t STALE_MS = 30000;
    static const uint32_t HOLD_MS = 2500;
    static const int32_t SWITCH_MARGIN = 180;

    std::array<Metric, BEARER_COUNT> _m;
    bool _haveLast;
    Bearer _last;
    uint32_t _lastDecisionMs;

    int32_t score(Bearer b, uint32_t nowMs) const;
    static uint16_t classBonus(LinkClass c);
};

class ResourceScheduler {
public:
    enum State : uint8_t {
        IDLE = 0,
        PROBING,
        TX,
        WAIT_ACK,
        RESTORING,
        COOLDOWN
    };

    ResourceScheduler();

    bool acquire(Resource r, Bearer owner, uint32_t nowMs, uint32_t leaseMs);
    void release(Resource r, Bearer owner);
    void setState(Resource r, State state, uint32_t nowMs);
    void tick(uint32_t nowMs);

    bool busy(Resource r) const;
    Bearer owner(Resource r) const;
    State state(Resource r) const;

private:
    struct Slot {
        bool locked;
        Bearer owner;
        State state;
        uint32_t deadlineMs;
        Slot();
    };
    Slot _slots[2];
};

} // namespace v10
} // namespace ops
