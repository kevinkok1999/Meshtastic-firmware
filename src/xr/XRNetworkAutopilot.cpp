#include "XRNetworkAutopilot.h"

#include <algorithm>

namespace meshoffgrid::xr {

namespace {
uint16_t ewma16(uint16_t oldValue, uint16_t sample)
{
    if (oldValue == 0)
        return sample;
    return static_cast<uint16_t>((static_cast<uint32_t>(oldValue) * 3u + sample) / 4u);
}

int16_t ewmaRssi(int16_t oldValue, int16_t sample)
{
    if (oldValue <= -126)
        return sample;
    return static_cast<int16_t>((static_cast<int32_t>(oldValue) * 3 + sample) / 4);
}
}

XRNetworkAutopilot::XRNetworkAutopilot(const XRNetworkAutopilotPolicy &policy) : policy_(policy) {}

XRNetworkAccessClass XRNetworkAutopilot::classify(const XRNetworkObservation &observation) const
{
    if (observation.passpointOrRoamingProfileMatched)
        return XRNetworkAccessClass::PASSPOINT_OR_ROAMING;

    if (observation.captivePortalDetected) {
        return observation.priorPortalAuthorizationValid ? XRNetworkAccessClass::PREAUTHORIZED_PORTAL
                                                         : XRNetworkAccessClass::REQUIRES_USER_INTERACTION;
    }

    if (observation.layer2Open)
        return XRNetworkAccessClass::OPEN_NO_PORTAL;

    return XRNetworkAccessClass::UNKNOWN;
}

uint8_t XRNetworkAutopilot::clampScore(int score)
{
    return static_cast<uint8_t>(std::max(0, std::min(100, score)));
}

const XRNetworkHistory *XRNetworkAutopilot::find(uint64_t networkKey) const
{
    for (const auto &entry : history_) {
        if (entry.used && entry.networkKey == networkKey)
            return &entry;
    }
    return nullptr;
}

const XRNetworkHistory *XRNetworkAutopilot::history(uint64_t networkKey) const
{
    return find(networkKey);
}

XRNetworkHistory *XRNetworkAutopilot::findOrAllocate(uint64_t networkKey)
{
    for (auto &entry : history_) {
        if (entry.used && entry.networkKey == networkKey)
            return &entry;
    }

    for (auto &entry : history_) {
        if (!entry.used) {
            entry = XRNetworkHistory{};
            entry.used = true;
            entry.networkKey = networkKey;
            return &entry;
        }
    }

    // Evict the least recently useful record. This keeps the model bounded and
    // avoids allowing network-learning state to grow without limit.
    auto *victim = &history_[0];
    uint32_t oldest = victim->lastSuccessMs;
    for (auto &entry : history_) {
        if (entry.lastSuccessMs < oldest) {
            oldest = entry.lastSuccessMs;
            victim = &entry;
        }
    }
    *victim = XRNetworkHistory{};
    victim->used = true;
    victim->networkKey = networkKey;
    return victim;
}

XRNetworkDecision XRNetworkAutopilot::evaluate(const XRNetworkObservation &observation, uint8_t batteryPercent,
                                                uint32_t nowMs) const
{
    XRNetworkDecision decision{};
    decision.accessClass = classify(observation);

    if (!policy_.enabled) {
        decision.rejectReason = XRNetworkRejectReason::AUTOPILOT_DISABLED;
        return decision;
    }
    if (batteryPercent < policy_.minimumBatteryPercent) {
        decision.rejectReason = XRNetworkRejectReason::BATTERY_TOO_LOW;
        return decision;
    }
    if (observation.rssiDbm < policy_.minimumRssiDbm) {
        decision.rejectReason = XRNetworkRejectReason::SIGNAL_TOO_WEAK;
        return decision;
    }

    switch (decision.accessClass) {
    case XRNetworkAccessClass::OPEN_NO_PORTAL:
        if (!policy_.allowOpenNoPortal) {
            decision.rejectReason = XRNetworkRejectReason::ACCESS_NOT_ALLOWED;
            return decision;
        }
        break;
    case XRNetworkAccessClass::PREAUTHORIZED_PORTAL:
        if (!policy_.allowPreauthorizedPortal) {
            decision.rejectReason = XRNetworkRejectReason::ACCESS_NOT_ALLOWED;
            return decision;
        }
        break;
    case XRNetworkAccessClass::PASSPOINT_OR_ROAMING:
        if (!policy_.allowPasspointOrRoaming) {
            decision.rejectReason = XRNetworkRejectReason::ACCESS_NOT_ALLOWED;
            return decision;
        }
        break;
    case XRNetworkAccessClass::REQUIRES_USER_INTERACTION:
        decision.rejectReason = XRNetworkRejectReason::PORTAL_REQUIRES_INTERACTION;
        return decision;
    default:
        decision.rejectReason = XRNetworkRejectReason::ACCESS_NOT_ALLOWED;
        return decision;
    }

    if (policy_.requireInternetVerification && !observation.internetVerified) {
        decision.rejectReason = XRNetworkRejectReason::INTERNET_UNVERIFIED;
        return decision;
    }
    if (policy_.requireSecureMqtt && !observation.secureMqttReachable) {
        decision.rejectReason = XRNetworkRejectReason::SECURE_TRANSPORT_UNAVAILABLE;
        return decision;
    }

    const auto *h = find(observation.networkKey);
    if (h && h->backoffUntilMs != 0 && static_cast<int32_t>(h->backoffUntilMs - nowMs) > 0) {
        decision.rejectReason = XRNetworkRejectReason::BACKOFF_ACTIVE;
        return decision;
    }

    int score = 35;
    if (decision.accessClass == XRNetworkAccessClass::PASSPOINT_OR_ROAMING)
        score += 20;
    else if (decision.accessClass == XRNetworkAccessClass::PREAUTHORIZED_PORTAL)
        score += 12;
    else if (decision.accessClass == XRNetworkAccessClass::OPEN_NO_PORTAL)
        score += 8;

    // RSSI contribution: -82 -> 0, -50 or better -> ~24.
    score += std::max(0, std::min(24, (observation.rssiDbm - policy_.minimumRssiDbm) * 24 / 32));

    if (observation.estimatedLatencyMs > 0) {
        if (observation.estimatedLatencyMs <= 100)
            score += 10;
        else if (observation.estimatedLatencyMs <= 300)
            score += 6;
        else if (observation.estimatedLatencyMs <= 800)
            score += 2;
        else
            score -= 8;
    }

    score -= std::min<int>(20, observation.packetLossPercent / 4);

    if (h) {
        const uint32_t sessions = static_cast<uint32_t>(h->successfulSessions) + h->failedSessions;
        if (sessions > 0) {
            const int successPct = static_cast<int>(100u * h->successfulSessions / sessions);
            score += (successPct - 50) / 4; // -12..+12 around neutral 50%
        }
        score += std::min<int>(10, h->successfulDeliveries / 2);
        score -= std::min<int>(15, h->mqttFailures * 2);
    }

    decision.score = clampScore(score);
    decision.connect = decision.score >= 55;
    if (!decision.connect)
        decision.rejectReason = XRNetworkRejectReason::RECENT_FAILURES;
    return decision;
}

void XRNetworkAutopilot::recordSessionResult(uint64_t networkKey, bool connected, bool internetVerified,
                                              bool secureMqttReady, int16_t rssiDbm, uint16_t latencyMs,
                                              uint32_t nowMs)
{
    if (networkKey == 0)
        return;
    auto *h = findOrAllocate(networkKey);
    h->ewmaRssiDbm = ewmaRssi(h->ewmaRssiDbm, rssiDbm);
    if (latencyMs)
        h->ewmaLatencyMs = ewma16(h->ewmaLatencyMs, latencyMs);

    const bool success = connected && internetVerified && (!policy_.requireSecureMqtt || secureMqttReady);
    if (success) {
        if (h->successfulSessions != UINT16_MAX)
            ++h->successfulSessions;
        h->lastSuccessMs = nowMs;
        h->backoffUntilMs = 0;
    } else {
        if (h->failedSessions != UINT16_MAX)
            ++h->failedSessions;
        if (connected && internetVerified && !secureMqttReady && h->mqttFailures != UINT16_MAX)
            ++h->mqttFailures;
        h->lastFailureMs = nowMs;

        const uint8_t weight = static_cast<uint8_t>(std::min<uint16_t>(policy_.maxConsecutiveFailureWeight, h->failedSessions));
        uint32_t delay = policy_.baseFailureBackoffMs;
        for (uint8_t i = 1; i < weight && delay < policy_.maxFailureBackoffMs; ++i)
            delay = std::min(policy_.maxFailureBackoffMs, delay * 2u);
        h->backoffUntilMs = nowMs + delay;
    }
}

void XRNetworkAutopilot::recordDeliveryResult(uint64_t networkKey, bool delivered, uint32_t nowMs)
{
    if (networkKey == 0)
        return;
    auto *h = findOrAllocate(networkKey);
    if (delivered) {
        if (h->successfulDeliveries != UINT16_MAX)
            ++h->successfulDeliveries;
        h->lastSuccessMs = nowMs;
    } else {
        if (h->mqttFailures != UINT16_MAX)
            ++h->mqttFailures;
        h->lastFailureMs = nowMs;
    }
}

int XRNetworkAutopilot::selectBest(const XRNetworkObservation *observations, size_t count, uint8_t batteryPercent,
                                   uint32_t nowMs, XRNetworkDecision *decision) const
{
    if (!observations || count == 0)
        return -1;

    int bestIndex = -1;
    XRNetworkDecision best{};
    for (size_t i = 0; i < count; ++i) {
        const auto current = evaluate(observations[i], batteryPercent, nowMs);
        if (!current.connect)
            continue;
        if (bestIndex < 0 || current.score > best.score) {
            bestIndex = static_cast<int>(i);
            best = current;
        }
    }

    if (decision)
        *decision = best;
    return bestIndex;
}

} // namespace meshoffgrid::xr
