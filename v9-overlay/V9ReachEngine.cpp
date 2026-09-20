#include "V9ReachEngine.h"

#include <algorithm>

namespace ops {

V9ReachEngine::V9ReachEngine() {
    auto& lora = _links[index(V9Route::LoRa)];
    lora.deliveryPermille = 790;
    lora.lossPermille = 210;
    lora.latencyMs = 900;
    lora.airtimeCost = 700;
    lora.energyCost = 600;
    lora.linkClass = V9LinkClass::Good;

    auto& esp = _links[index(V9Route::EspNowLR)];
    esp.deliveryPermille = 850;
    esp.lossPermille = 150;
    esp.latencyMs = 130;
    esp.airtimeCost = 250;
    esp.energyCost = 420;
    esp.linkClass = V9LinkClass::Good;

    auto& xbee = _links[index(V9Route::XBeeXR868)];
    xbee.deliveryPermille = 800;
    xbee.lossPermille = 200;
    xbee.latencyMs = 350;
    xbee.airtimeCost = 520;
    xbee.energyCost = 650;

    auto& lr = _links[index(V9Route::LR2021)];
    lr.deliveryPermille = 800;
    lr.lossPermille = 200;
    lr.latencyMs = 450;
    lr.airtimeCost = 560;
    lr.energyCost = 560;
}

void V9ReachEngine::setAvailable(V9Route route, bool available, uint32_t nowMs) {
    auto& m = _links[index(route)];
    m.available = available;
    m.lastUpdateMs = nowMs;
    if (!available) {
        m.reachable = false;
        m.failureStreak = 0;
        m.linkClass = V9LinkClass::Unknown;
    }
}

void V9ReachEngine::setReachable(V9Route route, bool reachable, uint32_t nowMs) {
    auto& m = _links[index(route)];
    m.reachable = reachable;
    m.lastUpdateMs = nowMs;
    if (reachable && m.deliveryPermille < 250) m.deliveryPermille = 250;
}

void V9ReachEngine::setLinkClass(V9Route route, V9LinkClass linkClass, uint32_t nowMs) {
    auto& m = _links[index(route)];
    m.linkClass = linkClass;
    m.lastUpdateMs = nowMs;
}

void V9ReachEngine::setQueueDepth(V9Route route, uint16_t depth, uint32_t nowMs) {
    auto& m = _links[index(route)];
    m.queueDepth = depth;
    m.lastUpdateMs = nowMs;
}

void V9ReachEngine::setHopCount(V9Route route, uint8_t hops, uint32_t nowMs) {
    auto& m = _links[index(route)];
    m.hopCount = hops;
    m.lastUpdateMs = nowMs;
}

void V9ReachEngine::noteSuccess(V9Route route, uint16_t latencyMs, uint32_t nowMs) {
    auto& m = _links[index(route)];
    m.available = true;
    m.reachable = true;
    m.deliveryPermille = static_cast<uint16_t>((static_cast<uint32_t>(m.deliveryPermille) * 7U + 3000U) / 10U);
    m.lossPermille = static_cast<uint16_t>((static_cast<uint32_t>(m.lossPermille) * 7U) / 10U);
    m.latencyMs = static_cast<uint16_t>((static_cast<uint32_t>(m.latencyMs) * 7U + static_cast<uint32_t>(latencyMs) * 3U) / 10U);
    m.retryPermille = static_cast<uint16_t>((static_cast<uint32_t>(m.retryPermille) * 8U) / 10U);
    m.failureStreak = 0;
    if (m.deliveryPermille >= 900) m.linkClass = V9LinkClass::Strong;
    else if (m.deliveryPermille >= 760) m.linkClass = V9LinkClass::Good;
    else m.linkClass = V9LinkClass::Marginal;
    m.lastUpdateMs = nowMs;
}

void V9ReachEngine::noteFailure(V9Route route, uint32_t nowMs) {
    auto& m = _links[index(route)];
    m.deliveryPermille = static_cast<uint16_t>((static_cast<uint32_t>(m.deliveryPermille) * 7U) / 10U);
    m.lossPermille = static_cast<uint16_t>(std::min<uint32_t>(1000U, (static_cast<uint32_t>(m.lossPermille) * 7U + 3000U) / 10U));
    if (m.failureStreak < 255) ++m.failureStreak;
    if (m.failureStreak >= 2) m.linkClass = V9LinkClass::Poor;
    if (m.failureStreak >= 3 && route != V9Route::LoRa) m.reachable = false;
    m.lastUpdateMs = nowMs;
}

void V9ReachEngine::noteRetry(V9Route route, uint32_t nowMs) {
    auto& m = _links[index(route)];
    m.retryPermille = static_cast<uint16_t>(std::min<uint32_t>(1000U, (static_cast<uint32_t>(m.retryPermille) * 8U + 2000U) / 10U));
    m.lastUpdateMs = nowMs;
}

uint16_t V9ReachEngine::classBonus(V9LinkClass linkClass) {
    switch (linkClass) {
        case V9LinkClass::Strong: return 700;
        case V9LinkClass::Good: return 450;
        case V9LinkClass::Marginal: return 120;
        case V9LinkClass::Poor: return 0;
        default: return 40;
    }
}

bool V9ReachEngine::allowed(V9Route route) const {
    switch (_mode) {
        case V9Mode::LoRaOnly: return route == V9Route::LoRa;
        case V9Mode::EspNowOnly: return route == V9Route::EspNowLR;
        case V9Mode::XBeeOnly: return route == V9Route::XBeeXR868;
        case V9Mode::LR2021Only: return route == V9Route::LR2021;
        default: return true;
    }
}

int32_t V9ReachEngine::score(V9Route route, uint32_t nowMs) const {
    if (!allowed(route)) return REJECTED;
    const auto& m = _links[index(route)];
    if (!m.available || !m.reachable) return REJECTED;
    if (m.lastUpdateMs != 0 &&
        static_cast<uint32_t>(nowMs - m.lastUpdateMs) > METRIC_MAX_AGE_MS &&
        route != V9Route::LoRa) return REJECTED;

    int32_t s = static_cast<int32_t>(m.deliveryPermille) * 7;
    s += classBonus(m.linkClass);
    s -= static_cast<int32_t>(m.lossPermille) * 3;
    s -= std::min<int32_t>(m.latencyMs, 3000);
    s -= std::min<int32_t>(m.retryPermille * 2, 1800);
    s -= std::min<int32_t>(static_cast<int32_t>(m.queueDepth) * 100, 1500);
    s -= std::min<int32_t>(m.airtimeCost, 1200);
    s -= std::min<int32_t>(m.energyCost, 1000);
    s -= static_cast<int32_t>(m.failureStreak) * 350;
    s -= static_cast<int32_t>(m.hopCount) * 120;

    switch (_mode) {
        case V9Mode::RangeFirst:
            s += static_cast<int32_t>(m.deliveryPermille) * 2;
            s += classBonus(m.linkClass);
            break;
        case V9Mode::PowerSave:
            s -= static_cast<int32_t>(m.energyCost) * 2;
            break;
        case V9Mode::LowLatency:
            s -= std::min<int32_t>(m.latencyMs * 2, 3500);
            if (route == V9Route::EspNowLR) s += 250;
            break;
        case V9Mode::Auto:
        case V9Mode::Redundant:
            if (route == V9Route::EspNowLR) s += 120;
            break;
        default:
            break;
    }
    return s;
}

V9ReachDecision V9ReachEngine::decide(uint32_t nowMs) {
    V9ReachDecision d{};
    std::array<std::pair<int32_t,V9Route>, static_cast<size_t>(V9Route::Count)> ranked{};
    for (size_t i = 0; i < ranked.size(); ++i) {
        const V9Route r = static_cast<V9Route>(i);
        ranked[i] = {score(r, nowMs), r};
    }
    std::sort(ranked.begin(), ranked.end(), [](const std::pair<int32_t, V9Route>& a, const std::pair<int32_t, V9Route>& b) { return a.first > b.first; });

    if (ranked[0].first != REJECTED) {
        d.hasPrimary = true;
        d.primary = ranked[0].second;
        d.primaryScore = ranked[0].first;
    }
    if (ranked[1].first != REJECTED) {
        d.hasBackup = true;
        d.backup = ranked[1].second;
        d.backupScore = ranked[1].first;
    }

    if (_haveLast && d.hasPrimary && _lastPrimary != d.primary &&
        static_cast<uint32_t>(nowMs - _lastDecisionMs) < HYSTERESIS_WINDOW_MS) {
        const int32_t previousScore = score(_lastPrimary, nowMs);
        if (previousScore != REJECTED && d.primaryScore < previousScore + HYSTERESIS_MARGIN) {
            if (d.primary != _lastPrimary) {
                d.backup = d.primary;
                d.backupScore = d.primaryScore;
                d.hasBackup = true;
            }
            d.primary = _lastPrimary;
            d.primaryScore = previousScore;
        }
    }

    if (d.hasPrimary) {
        const auto& m = _links[index(d.primary)];
        d.useFec = m.linkClass == V9LinkClass::Poor || m.linkClass == V9LinkClass::Marginal || m.lossPermille >= 220;
        if (m.linkClass == V9LinkClass::Strong) {
            d.fragmentBytes = 160;
            d.retryRounds = 1;
        } else if (m.linkClass == V9LinkClass::Good) {
            d.fragmentBytes = 128;
            d.retryRounds = 2;
        } else if (m.linkClass == V9LinkClass::Marginal) {
            d.fragmentBytes = 80;
            d.retryRounds = 3;
        } else {
            d.fragmentBytes = 56;
            d.retryRounds = 4;
        }
    }

    d.redundant = (_mode == V9Mode::Redundant) && d.hasPrimary && d.hasBackup;
    d.allowStoreForward = true;

    if (d.hasPrimary) {
        _lastPrimary = d.primary;
        _lastDecisionMs = nowMs;
        _haveLast = true;
    }
    return d;
}

const V9LinkMetric& V9ReachEngine::metrics(V9Route route) const {
    return _links[index(route)];
}

const char* V9ReachEngine::name(V9Route route) {
    switch (route) {
        case V9Route::LoRa: return "LoRa";
        case V9Route::EspNowLR: return "ESP-NOW LR";
        case V9Route::XBeeXR868: return "XBee XR 868";
        case V9Route::LR2021: return "LR2021";
        default: return "Unknown";
    }
}

} // namespace ops
