#include "XRAdaptiveIntelligence.h"

#include <algorithm>
#include <climits>

namespace meshoffgrid::xr {

namespace {
uint16_t satInc16(uint16_t value)
{
    return value == UINT16_MAX ? value : static_cast<uint16_t>(value + 1u);
}

int16_t clampReward(int32_t value)
{
    return static_cast<int16_t>(std::max<int32_t>(-100, std::min<int32_t>(100, value)));
}

int16_t explorationBonus(uint16_t samples)
{
    if (samples == 0)
        return 24;
    if (samples < 2)
        return 18;
    if (samples < 4)
        return 12;
    if (samples < 8)
        return 8;
    if (samples < 16)
        return 4;
    return 2;
}
} // namespace

XRAdaptiveIntelligence::XRAdaptiveIntelligence(const XRAdaptivePolicy &policy) : policy_(policy) {}

uint8_t XRAdaptiveIntelligence::bucketFor(const XRAdaptiveContext &context,
                                           const XRAdaptiveCapabilities &capabilities) const
{
    uint8_t rfBucket = 0;
    if (context.rfLinkScore >= 70)
        rfBucket = 2;
    else if (context.rfLinkScore >= 35)
        rfBucket = 1;

    const bool wifiUsable = capabilities.wifiMqttAvailable && capabilities.wifiMqttPrivacyApproved &&
                            context.networkAutopilotScore >= 55;
    const uint8_t wifiBucket = wifiUsable ? 1 : 0;
    const uint8_t batteryBucket = context.batteryPercent < 25 ? 0 : 1;
    const uint8_t ageBucket = context.pendingAgeMs >= 60000u ? 1 : 0;

    return static_cast<uint8_t>((((rfBucket * WIFI_BUCKETS) + wifiBucket) * BATTERY_BUCKETS + batteryBucket) *
                                    AGE_BUCKETS +
                                ageBucket);
}

bool XRAdaptiveIntelligence::actionAllowed(XRAdaptiveAction action, const XRAdaptiveContext &context,
                                           const XRAdaptiveCapabilities &capabilities, uint32_t nowMs,
                                           uint8_t bucket) const
{
    const auto &arm = arms_[bucket][actionIndex(action)];
    if (action != XRAdaptiveAction::BASELINE && arm.quarantineUntilMs != 0 &&
        static_cast<int32_t>(arm.quarantineUntilMs - nowMs) > 0)
        return false;

    switch (action) {
    case XRAdaptiveAction::BASELINE:
        return true;
    case XRAdaptiveAction::LORA_PREFERRED:
        return capabilities.loraAvailable;
    case XRAdaptiveAction::WIFI_MQTT_PREFERRED:
        return capabilities.wifiMqttAvailable && capabilities.wifiMqttPrivacyApproved &&
               context.networkAutopilotScore >= 55 && context.batteryPercent >= policy_.minimumBatteryForWifi;
    case XRAdaptiveAction::ESP_NOW_PREFERRED:
        return capabilities.espNowAvailable && capabilities.espNowPrivacyApproved && context.espNowLinkScore >= 35 &&
               context.batteryPercent >= policy_.minimumBatteryForEspNow;
    case XRAdaptiveAction::XBEE_PREFERRED:
        return capabilities.xbeeAvailable && capabilities.xbeePrivacyApproved && context.xbeeLinkScore >= 30 &&
               context.batteryPercent >= policy_.minimumBatteryForXBee;
    case XRAdaptiveAction::LORA_RX_FOCUS:
        return capabilities.loraAvailable && capabilities.rxFocusAvailable &&
               context.batteryPercent >= policy_.minimumBatteryForRxFocus;
    case XRAdaptiveAction::QUIET_BACKGROUND:
        return context.pendingAgeMs > 0 || context.recentAckFailures > 0 || context.recentDeliveryFailures > 0;
    case XRAdaptiveAction::RECOVERY_WINDOW:
        return capabilities.loraAvailable && capabilities.recoveryWindowAvailable && context.pendingAgeMs >= 30000u;
    case XRAdaptiveAction::COURIER_WAIT:
        return capabilities.courierAvailable && context.pendingAgeMs >= 60000u &&
               context.batteryPercent >= policy_.minimumBatteryForCourier;
    case XRAdaptiveAction::COUNT:
        return false;
    }
    return false;
}

bool XRAdaptiveIntelligence::evidenceFresh(const XRAdaptiveArmState &state, uint32_t nowMs) const
{
    if (state.samples == 0 || state.lastUpdateMs == 0)
        return false;
    return nowMs - state.lastUpdateMs <= policy_.knowledgeFreshMs;
}

bool XRAdaptiveIntelligence::isPromoted(uint8_t bucket, XRAdaptiveAction action, uint32_t nowMs) const
{
    if (action == XRAdaptiveAction::BASELINE)
        return true;

    const auto &candidate = arms_[bucket][actionIndex(action)];
    const auto &baseline = arms_[bucket][actionIndex(XRAdaptiveAction::BASELINE)];

    if (candidate.samples < policy_.minimumSamplesForPromotion || baseline.samples < policy_.minimumSamplesForPromotion)
        return false;
    if (!evidenceFresh(candidate, nowMs) || !evidenceFresh(baseline, nowMs))
        return false;

    return candidate.rewardEwma >= baseline.rewardEwma + policy_.minimumRewardImprovement &&
           candidate.failureStreak < policy_.rollbackFailureStreak;
}

uint32_t XRAdaptiveIntelligence::mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

XRAdaptiveDecision XRAdaptiveIntelligence::choose(const XRAdaptiveContext &context,
                                                   const XRAdaptiveCapabilities &capabilities, uint32_t nowMs,
                                                   uint32_t decisionNonce) const
{
    XRAdaptiveDecision result{};
    const uint8_t bucket = bucketFor(context, capabilities);
    result.contextBucket = bucket;

    if (!policy_.enabled) {
        result.action = XRAdaptiveAction::BASELINE;
        result.expectedReward = arms_[bucket][actionIndex(result.action)].rewardEwma;
        return result;
    }

    XRAdaptiveAction best = XRAdaptiveAction::BASELINE;
    int16_t bestReward = arms_[bucket][actionIndex(best)].rewardEwma;

    for (uint8_t i = 1; i < actionIndex(XRAdaptiveAction::COUNT); ++i) {
        const auto action = static_cast<XRAdaptiveAction>(i);
        if (!actionAllowed(action, context, capabilities, nowMs, bucket) || !isPromoted(bucket, action, nowMs))
            continue;
        const auto reward = arms_[bucket][i].rewardEwma;
        if (reward > bestReward) {
            best = action;
            bestReward = reward;
        }
    }

    result.action = best;
    result.expectedReward = bestReward;
    result.promoted = best != XRAdaptiveAction::BASELINE;

    if (policy_.explorationPercent == 0)
        return result;

    const uint32_t ticket = mix32(decisionNonce ^ nowMs ^ (static_cast<uint32_t>(bucket) << 24) ^ modelEpoch_);
    if ((ticket % 100u) >= policy_.explorationPercent)
        return result;

    XRAdaptiveAction explore = XRAdaptiveAction::BASELINE;
    int16_t exploreScore = INT16_MIN;
    for (uint8_t i = 1; i < actionIndex(XRAdaptiveAction::COUNT); ++i) {
        const auto action = static_cast<XRAdaptiveAction>(i);
        if (!actionAllowed(action, context, capabilities, nowMs, bucket))
            continue;

        const auto &arm = arms_[bucket][i];
        int16_t score = static_cast<int16_t>(arm.rewardEwma + explorationBonus(arm.samples));
        if (arm.samples > 0 && !evidenceFresh(arm, nowMs))
            score = static_cast<int16_t>(std::min<int>(INT16_MAX, score + 6));

        if (explore == XRAdaptiveAction::BASELINE || score > exploreScore) {
            explore = action;
            exploreScore = score;
        }
    }

    if (explore != XRAdaptiveAction::BASELINE) {
        result.action = explore;
        result.expectedReward = arms_[bucket][actionIndex(explore)].rewardEwma;
        result.exploratory = true;
        result.promoted = isPromoted(bucket, explore, nowMs);
    }

    return result;
}

int16_t XRAdaptiveIntelligence::scoreOutcome(const XRAdaptiveOutcome &outcome)
{
    if (outcome.privacyRejected)
        return -100;

    // A sidecar handoff is positive evidence, but deliberately much weaker
    // than true end-to-end delivery. This keeps ESP-NOW/Wi-Fi link learning
    // useful without falsely teaching the model that a chat message was ACKed.
    int32_t reward = outcome.delivered ? 70 : (outcome.transportAccepted ? 15 : -55);
    if (outcome.acked)
        reward += 20;
    if (outcome.transportFailed)
        reward -= 20;
    if (outcome.duplicateObserved)
        reward -= 25;

    reward -= std::min<int32_t>(15, outcome.latencyMs / 1000u);
    reward -= std::min<int32_t>(10, outcome.airtimeMs / 500u);
    reward -= std::min<int32_t>(10, outcome.estimatedEnergyMilliJoules / 250u);
    reward -= std::min<int32_t>(10, outcome.retries * 2u);

    return clampReward(reward);
}

void XRAdaptiveIntelligence::learn(const XRAdaptiveDecision &decision, const XRAdaptiveOutcome &outcome, uint32_t nowMs)
{
    if (decision.contextBucket >= CONTEXT_BUCKETS || decision.action == XRAdaptiveAction::COUNT)
        return;

    auto &arm = arms_[decision.contextBucket][actionIndex(decision.action)];
    const int16_t reward = scoreOutcome(outcome);

    arm.samples = satInc16(arm.samples);
    if (outcome.delivered)
        arm.deliveries = satInc16(arm.deliveries);
    else if (!outcome.transportAccepted)
        arm.failures = satInc16(arm.failures);

    if (arm.samples == 1)
        arm.rewardEwma = reward;
    else
        arm.rewardEwma = static_cast<int16_t>((static_cast<int32_t>(arm.rewardEwma) * 7 + reward) / 8);

    const bool failed = (!outcome.delivered && !outcome.transportAccepted) || outcome.transportFailed ||
                        outcome.privacyRejected;
    if (failed) {
        if (arm.failureStreak != UINT8_MAX)
            ++arm.failureStreak;
        if (decision.action != XRAdaptiveAction::BASELINE && arm.failureStreak >= policy_.rollbackFailureStreak)
            arm.quarantineUntilMs = nowMs + policy_.quarantineMs;
    } else {
        arm.failureStreak = 0;
        arm.quarantineUntilMs = 0;
    }

    arm.lastUpdateMs = nowMs;
    ++modelEpoch_;
    dirty_ = true;
}

const XRAdaptiveArmState &XRAdaptiveIntelligence::state(uint8_t contextBucket, XRAdaptiveAction action) const
{
    static const XRAdaptiveArmState empty{};
    if (contextBucket >= CONTEXT_BUCKETS || action == XRAdaptiveAction::COUNT)
        return empty;
    return arms_[contextBucket][actionIndex(action)];
}

uint32_t XRAdaptiveIntelligence::checksumSnapshot(const Snapshot &snapshot)
{
    constexpr uint32_t FNV_OFFSET = 2166136261u;
    constexpr uint32_t FNV_PRIME = 16777619u;
    uint32_t hash = FNV_OFFSET;

    auto feed8 = [&hash](uint8_t value) {
        hash ^= value;
        hash *= FNV_PRIME;
    };
    auto feed16 = [&feed8](uint16_t value) {
        feed8(static_cast<uint8_t>(value & 0xffu));
        feed8(static_cast<uint8_t>((value >> 8) & 0xffu));
    };
    auto feed32 = [&feed8](uint32_t value) {
        for (uint8_t shift = 0; shift < 32; shift += 8)
            feed8(static_cast<uint8_t>((value >> shift) & 0xffu));
    };

    feed32(snapshot.magic);
    feed16(snapshot.version);
    feed16(snapshot.reserved);
    feed32(snapshot.modelEpoch);
    for (const auto &bucket : snapshot.arms) {
        for (const auto &arm : bucket) {
            feed16(arm.samples);
            feed16(arm.deliveries);
            feed16(arm.failures);
            feed16(static_cast<uint16_t>(arm.rewardEwma));
            feed8(arm.failureStreak);
            feed32(arm.quarantineUntilMs);
            feed32(arm.lastUpdateMs);
        }
    }
    return hash;
}

XRAdaptiveIntelligence::Snapshot XRAdaptiveIntelligence::snapshot() const
{
    Snapshot out{};
    out.modelEpoch = modelEpoch_;
    out.arms = arms_;
    out.checksum = checksumSnapshot(out);
    return out;
}

bool XRAdaptiveIntelligence::restore(const Snapshot &input)
{
    if (input.magic != 0x58524149 || input.version != 4 || input.checksum != checksumSnapshot(input))
        return false;

    arms_ = input.arms;
    modelEpoch_ = input.modelEpoch;
    dirty_ = false;
    return true;
}

bool XRAdaptiveIntelligence::shouldPersist(uint32_t nowMs) const
{
    return dirty_ && (lastPersistMs_ == 0 || nowMs - lastPersistMs_ >= policy_.minimumPersistIntervalMs);
}

} // namespace meshoffgrid::xr
