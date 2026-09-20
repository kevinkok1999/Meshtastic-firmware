#include "V10DirectLinkCore.h"

#include <algorithm>

namespace ops {
namespace v10 {

Metric::Metric()
    : available(false), reachable(false), deliveryPermille(650), latencyMs(900),
      retryPermille(0), transitionCost(200), energyCost(500), failures(0),
      linkClass(LINK_UNKNOWN), updatedMs(0) {}

Decision::Decision()
    : hasPrimary(false), hasBackup(false), primary(BEARER_LORA), backup(BEARER_LORA),
      primaryScore(-1000000), backupScore(-1000000), retryRounds(2),
      useFec(false), fragmentBytes(96) {}

ResourceScheduler::Slot::Slot()
    : locked(false), owner(BEARER_LORA), state(IDLE), deadlineMs(0) {}

ResourceScheduler::ResourceScheduler() {}

bool ResourceScheduler::acquire(Resource r, Bearer ownerValue, uint32_t nowMs, uint32_t leaseMs) {
    Slot& s = _slots[static_cast<uint8_t>(r)];
    if (s.locked && s.owner != ownerValue) return false;
    s.locked = true;
    s.owner = ownerValue;
    s.deadlineMs = nowMs + leaseMs;
    return true;
}

void ResourceScheduler::release(Resource r, Bearer ownerValue) {
    Slot& s = _slots[static_cast<uint8_t>(r)];
    if (!s.locked || s.owner != ownerValue) return;
    s = Slot();
}

void ResourceScheduler::setState(Resource r, State stateValue, uint32_t nowMs) {
    Slot& s = _slots[static_cast<uint8_t>(r)];
    s.state = stateValue;
    if (s.locked && s.deadlineMs == 0) s.deadlineMs = nowMs + 1000;
}

void ResourceScheduler::tick(uint32_t nowMs) {
    for (int i = 0; i < 2; ++i) {
        Slot& s = _slots[i];
        if (s.locked && s.deadlineMs != 0 && static_cast<int32_t>(nowMs - s.deadlineMs) >= 0) {
            s = Slot();
        }
    }
}

bool ResourceScheduler::busy(Resource r) const {
    return _slots[static_cast<uint8_t>(r)].locked;
}

Bearer ResourceScheduler::owner(Resource r) const {
    return _slots[static_cast<uint8_t>(r)].owner;
}

ResourceScheduler::State ResourceScheduler::state(Resource r) const {
    return _slots[static_cast<uint8_t>(r)].state;
}

DirectLinkBrain::DirectLinkBrain()
    : _haveLast(false), _last(BEARER_LORA), _lastDecisionMs(0) {
    _m[BEARER_LORA].deliveryPermille = 780;
    _m[BEARER_LORA].latencyMs = 950;
    _m[BEARER_LORA].transitionCost = 0;
    _m[BEARER_LORA].energyCost = 600;
    _m[BEARER_LORA].linkClass = LINK_GOOD;

    _m[BEARER_GFSK].deliveryPermille = 600;
    _m[BEARER_GFSK].latencyMs = 300;
    _m[BEARER_GFSK].transitionCost = 750;
    _m[BEARER_GFSK].energyCost = 520;

    _m[BEARER_ESPNOW_LR].deliveryPermille = 820;
    _m[BEARER_ESPNOW_LR].latencyMs = 180;
    _m[BEARER_ESPNOW_LR].transitionCost = 0;
    _m[BEARER_ESPNOW_LR].energyCost = 420;
    _m[BEARER_ESPNOW_LR].linkClass = LINK_GOOD;

    _m[BEARER_WIFI_LR].deliveryPermille = 700;
    _m[BEARER_WIFI_LR].latencyMs = 260;
    _m[BEARER_WIFI_LR].transitionCost = 180;
    _m[BEARER_WIFI_LR].energyCost = 540;
    _m[BEARER_WIFI_LR].linkClass = LINK_MARGINAL;

    _m[BEARER_BLE_CODED].deliveryPermille = 650;
    _m[BEARER_BLE_CODED].latencyMs = 450;
    _m[BEARER_BLE_CODED].transitionCost = 400;
    _m[BEARER_BLE_CODED].energyCost = 460;
}

void DirectLinkBrain::setAvailable(Bearer b, bool value, uint32_t nowMs) {
    Metric& m = _m[static_cast<uint8_t>(b)];
    m.available = value;
    m.updatedMs = nowMs;
    if (!value) {
        m.reachable = false;
        m.failures = 0;
    }
}

void DirectLinkBrain::setReachable(Bearer b, bool value, uint32_t nowMs) {
    Metric& m = _m[static_cast<uint8_t>(b)];
    m.reachable = value;
    m.updatedMs = nowMs;
}

void DirectLinkBrain::setLinkClass(Bearer b, LinkClass value, uint32_t nowMs) {
    Metric& m = _m[static_cast<uint8_t>(b)];
    m.linkClass = value;
    m.updatedMs = nowMs;
}

void DirectLinkBrain::noteSuccess(Bearer b, uint16_t latencyMs, uint32_t nowMs) {
    Metric& m = _m[static_cast<uint8_t>(b)];
    m.available = true;
    m.reachable = true;
    m.deliveryPermille = static_cast<uint16_t>(
        (static_cast<uint32_t>(m.deliveryPermille) * 7U + 3000U) / 10U);
    m.latencyMs = static_cast<uint16_t>(
        (static_cast<uint32_t>(m.latencyMs) * 7U + static_cast<uint32_t>(latencyMs) * 3U) / 10U);
    m.retryPermille = static_cast<uint16_t>((static_cast<uint32_t>(m.retryPermille) * 8U) / 10U);
    m.failures = 0;
    if (m.deliveryPermille >= 900) m.linkClass = LINK_STRONG;
    else if (m.deliveryPermille >= 760) m.linkClass = LINK_GOOD;
    else m.linkClass = LINK_MARGINAL;
    m.updatedMs = nowMs;
}

void DirectLinkBrain::noteFailure(Bearer b, uint32_t nowMs) {
    Metric& m = _m[static_cast<uint8_t>(b)];
    m.deliveryPermille = static_cast<uint16_t>(
        (static_cast<uint32_t>(m.deliveryPermille) * 7U) / 10U);
    if (m.failures < 255) ++m.failures;
    if (m.failures >= 2) m.linkClass = LINK_POOR;
    if (m.failures >= 3 && b != BEARER_LORA) m.reachable = false;
    m.updatedMs = nowMs;
}

void DirectLinkBrain::noteRetry(Bearer b, uint32_t nowMs) {
    Metric& m = _m[static_cast<uint8_t>(b)];
    m.retryPermille = static_cast<uint16_t>(
        std::min<uint32_t>(1000U, (static_cast<uint32_t>(m.retryPermille) * 8U + 2000U) / 10U));
    m.updatedMs = nowMs;
}

uint16_t DirectLinkBrain::classBonus(LinkClass c) {
    switch (c) {
        case LINK_STRONG: return 700;
        case LINK_GOOD: return 450;
        case LINK_MARGINAL: return 150;
        case LINK_POOR: return 0;
        default: return 30;
    }
}

Resource DirectLinkBrain::resource(Bearer b) {
    return (b == BEARER_LORA || b == BEARER_GFSK) ? RESOURCE_SUBGHZ : RESOURCE_RF24;
}

int32_t DirectLinkBrain::score(Bearer b, uint32_t nowMs) const {
    const Metric& m = _m[static_cast<uint8_t>(b)];
    if (!m.available || !m.reachable) return REJECTED;
    if (m.updatedMs != 0 && static_cast<uint32_t>(nowMs - m.updatedMs) > STALE_MS && b != BEARER_LORA) {
        return REJECTED;
    }

    int32_t s = static_cast<int32_t>(m.deliveryPermille) * 8;
    s += classBonus(m.linkClass);
    s -= std::min<int32_t>(m.latencyMs, 3000);
    s -= std::min<int32_t>(m.retryPermille * 2, 1600);
    s -= std::min<int32_t>(m.transitionCost * 2, 1600);
    s -= std::min<int32_t>(m.energyCost, 1000);
    s -= static_cast<int32_t>(m.failures) * 400;

    if (b == BEARER_LORA) s += 260;
    if (b == BEARER_WIFI_LR) s += 160;
    if (b == BEARER_ESPNOW_LR) s += 120;
    return s;
}

Decision DirectLinkBrain::decide(uint32_t nowMs) {
    Decision d;
    int32_t best = REJECTED;
    int32_t second = REJECTED;
    Bearer bestB = BEARER_LORA;
    Bearer secondB = BEARER_LORA;

    for (uint8_t i = 0; i < BEARER_COUNT; ++i) {
        const Bearer b = static_cast<Bearer>(i);
        const int32_t s = score(b, nowMs);
        if (s > best) {
            second = best;
            secondB = bestB;
            best = s;
            bestB = b;
        } else if (s > second) {
            second = s;
            secondB = b;
        }
    }

    if (best != REJECTED) {
        d.hasPrimary = true;
        d.primary = bestB;
        d.primaryScore = best;
    }

    if (second != REJECTED) {
        d.hasBackup = true;
        d.backup = secondB;
        d.backupScore = second;
    }

    if (_haveLast && d.hasPrimary && d.primary != _last &&
        static_cast<uint32_t>(nowMs - _lastDecisionMs) < HOLD_MS) {
        const int32_t oldScore = score(_last, nowMs);
        if (oldScore != REJECTED && d.primaryScore < oldScore + SWITCH_MARGIN) {
            d.backup = d.primary;
            d.backupScore = d.primaryScore;
            d.hasBackup = true;
            d.primary = _last;
            d.primaryScore = oldScore;
        }
    }

    if (d.hasPrimary && d.hasBackup && resource(d.primary) == resource(d.backup)) {
        int32_t diverseScore = REJECTED;
        Bearer diverse = d.backup;
        for (uint8_t i = 0; i < BEARER_COUNT; ++i) {
            const Bearer b = static_cast<Bearer>(i);
            if (resource(b) == resource(d.primary)) continue;
            const int32_t s = score(b, nowMs);
            if (s > diverseScore) {
                diverseScore = s;
                diverse = b;
            }
        }
        if (diverseScore != REJECTED && diverseScore + 250 >= d.backupScore) {
            d.backup = diverse;
            d.backupScore = diverseScore;
        }
    }

    if (d.hasPrimary) {
        const Metric& m = _m[static_cast<uint8_t>(d.primary)];
        if (m.linkClass == LINK_STRONG) {
            d.retryRounds = 1;
            d.fragmentBytes = 160;
        } else if (m.linkClass == LINK_GOOD) {
            d.retryRounds = 2;
            d.fragmentBytes = 128;
        } else if (m.linkClass == LINK_MARGINAL) {
            d.retryRounds = 3;
            d.fragmentBytes = 80;
            d.useFec = true;
        } else {
            d.retryRounds = 4;
            d.fragmentBytes = 56;
            d.useFec = true;
        }
        _last = d.primary;
        _lastDecisionMs = nowMs;
        _haveLast = true;
    }

    return d;
}

const Metric& DirectLinkBrain::metric(Bearer b) const {
    return _m[static_cast<uint8_t>(b)];
}

const char* DirectLinkBrain::name(Bearer b) {
    switch (b) {
        case BEARER_LORA: return "LoRa";
        case BEARER_GFSK: return "GFSK";
        case BEARER_ESPNOW_LR: return "ESP-NOW LR";
        case BEARER_WIFI_LR: return "Wi-Fi LR Direct";
        case BEARER_BLE_CODED: return "BLE Coded";
        default: return "Unknown";
    }
}

} // namespace v10
} // namespace ops
