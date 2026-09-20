#include "V8RouteCore.h"

#include <algorithm>

namespace ops {

V8RouteCore::V8RouteCore() {
    auto& lora = _links[index(V8Route::LoRa)];
    lora.deliveryPermille = 780;
    lora.latencyMs = 900;
    lora.energyCost = 600;

    auto& esp = _links[index(V8Route::EspNowLR)];
    esp.deliveryPermille = 850;
    esp.latencyMs = 120;
    esp.energyCost = 420;

    auto& xbee = _links[index(V8Route::XBeeXR868)];
    xbee.deliveryPermille = 800;
    xbee.latencyMs = 350;
    xbee.energyCost = 650;

    auto& lr = _links[index(V8Route::LR2021)];
    lr.deliveryPermille = 800;
    lr.latencyMs = 450;
    lr.energyCost = 560;
}

void V8RouteCore::setAvailable(V8Route route, bool available, uint32_t nowMs) {
    auto& m = _links[index(route)];
    m.available = available;
    m.lastUpdateMs = nowMs;
    if (!available) {
        m.reachable = false;
        m.failureStreak = 0;
    }
}

void V8RouteCore::setReachable(V8Route route, bool reachable, uint32_t nowMs) {
    auto& m = _links[index(route)];
    m.reachable = reachable;
    m.lastUpdateMs = nowMs;
    if (reachable && m.deliveryPermille < 300) m.deliveryPermille = 300;
}

void V8RouteCore::noteSuccess(V8Route route, uint16_t latencyMs, uint32_t nowMs) {
    auto& m = _links[index(route)];
    m.available = true;
    m.reachable = true;
    m.deliveryPermille = static_cast<uint16_t>((static_cast<uint32_t>(m.deliveryPermille) * 7U + 1000U * 3U) / 10U);
    m.latencyMs = static_cast<uint16_t>((static_cast<uint32_t>(m.latencyMs) * 7U + latencyMs * 3U) / 10U);
    m.failureStreak = 0;
    m.lastUpdateMs = nowMs;
}

void V8RouteCore::noteFailure(V8Route route, uint32_t nowMs) {
    auto& m = _links[index(route)];
    m.deliveryPermille = static_cast<uint16_t>((static_cast<uint32_t>(m.deliveryPermille) * 7U) / 10U);
    if (m.failureStreak < 255) ++m.failureStreak;
    if (m.failureStreak >= 3 && route != V8Route::LoRa) m.reachable = false;
    m.lastUpdateMs = nowMs;
}

void V8RouteCore::setQueueDepth(V8Route route, uint16_t depth, uint32_t nowMs) {
    auto& m = _links[index(route)];
    m.queueDepth = depth;
    m.lastUpdateMs = nowMs;
}

bool V8RouteCore::allowed(V8Route route) const {
    switch (_mode) {
        case V8RouteMode::LoRaOnly: return route == V8Route::LoRa;
        case V8RouteMode::EspNowOnly: return route == V8Route::EspNowLR;
        case V8RouteMode::XBeeOnly: return route == V8Route::XBeeXR868;
        case V8RouteMode::LR2021Only: return route == V8Route::LR2021;
        default: return true;
    }
}

int32_t V8RouteCore::score(V8Route route, uint32_t nowMs) const {
    if (!allowed(route)) return REJECTED;
    const auto& m = _links[index(route)];
    if (!m.available || !m.reachable) return REJECTED;
    if (m.lastUpdateMs != 0 && static_cast<uint32_t>(nowMs - m.lastUpdateMs) > METRIC_MAX_AGE_MS && route != V8Route::LoRa) {
        return REJECTED;
    }

    int32_t s = static_cast<int32_t>(m.deliveryPermille) * 6;
    s -= std::min<int32_t>(m.latencyMs, 2500);
    s -= std::min<int32_t>(static_cast<int32_t>(m.queueDepth) * 90, 1200);
    s -= std::min<int32_t>(m.energyCost, 1000);
    s -= static_cast<int32_t>(m.failureStreak) * 250;

    switch (_mode) {
        case V8RouteMode::RangeFirst:
            if (route == V8Route::LR2021) s += 420;
            if (route == V8Route::XBeeXR868) s += 320;
            if (route == V8Route::LoRa) s += 260;
            break;
        case V8RouteMode::PowerSave:
            s -= static_cast<int32_t>(m.energyCost) * 2;
            break;
        case V8RouteMode::AutoBalanced:
        case V8RouteMode::Redundant:
            if (route == V8Route::EspNowLR) s += 250;
            break;
        default:
            break;
    }
    return s;
}

V8Route V8RouteCore::choose(uint32_t nowMs) {
    V8Route best = V8Route::LoRa;
    int32_t bestScore = REJECTED;

    for (size_t i = 0; i < static_cast<size_t>(V8Route::Count); ++i) {
        const auto route = static_cast<V8Route>(i);
        const int32_t candidate = score(route, nowMs);
        if (candidate > bestScore) {
            bestScore = candidate;
            best = route;
        }
    }

    if (_haveLast && static_cast<uint32_t>(nowMs - _lastSelectMs) < STICKY_WINDOW_MS) {
        const int32_t previous = score(_lastRoute, nowMs);
        if (previous != REJECTED && bestScore < previous + HYSTERESIS_SCORE) {
            best = _lastRoute;
            bestScore = previous;
        }
    }

    _lastRoute = best;
    _lastScore = bestScore;
    _lastSelectMs = nowMs;
    _haveLast = bestScore != REJECTED;
    return best;
}

const V8LinkMetrics& V8RouteCore::metrics(V8Route route) const {
    return _links[index(route)];
}

const char* V8RouteCore::name(V8Route route) {
    switch (route) {
        case V8Route::LoRa: return "LoRa";
        case V8Route::EspNowLR: return "ESP-NOW LR";
        case V8Route::XBeeXR868: return "XBee XR 868";
        case V8Route::LR2021: return "LR2021";
        default: return "Unknown";
    }
}

} // namespace ops
