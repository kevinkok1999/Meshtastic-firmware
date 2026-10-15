#include "TestUtil.h"
#include "xr/XRAdaptiveIntelligence.h"
#include <cstdlib>
#include <unity.h>

using namespace meshoffgrid::xr;

void setUp(void) {}
void tearDown(void) {}

static XRAdaptiveContext goodWifiContext()
{
    XRAdaptiveContext c{};
    c.rfLinkScore = 20;
    c.channelHealthScore = 60;
    c.networkAutopilotScore = 90;
    c.batteryPercent = 80;
    c.pendingAgeMs = 5000;
    c.privatePayload = true;
    return c;
}

static XRAdaptiveCapabilities wifiOnlyCapabilities(bool privacyApproved = true)
{
    XRAdaptiveCapabilities caps{};
    caps.loraAvailable = false;
    caps.wifiMqttAvailable = true;
    caps.wifiMqttPrivacyApproved = privacyApproved;
    return caps;
}

void test_private_wifi_is_never_selected_without_privacy_approval()
{
    XRAdaptivePolicy p{};
    p.explorationPercent = 100;
    XRAdaptiveIntelligence ai(p);

    const auto d = ai.choose(goodWifiContext(), wifiOnlyCapabilities(false), 1000, 42);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(XRAdaptiveAction::BASELINE), static_cast<uint8_t>(d.action));
    TEST_ASSERT_FALSE(d.exploratory);
}

void test_success_reward_is_higher_than_failure_reward()
{
    XRAdaptiveOutcome success{};
    success.delivered = true;
    success.acked = true;
    success.latencyMs = 800;

    XRAdaptiveOutcome failure{};
    failure.delivered = false;
    failure.transportFailed = true;

    TEST_ASSERT_TRUE(XRAdaptiveIntelligence::scoreOutcome(success) > XRAdaptiveIntelligence::scoreOutcome(failure));
}

void test_privacy_rejection_is_absolute_negative_reward()
{
    XRAdaptiveOutcome outcome{};
    outcome.delivered = true;
    outcome.acked = true;
    outcome.privacyRejected = true;
    TEST_ASSERT_EQUAL_INT16(-100, XRAdaptiveIntelligence::scoreOutcome(outcome));
}

void test_three_failures_quarantine_nonbaseline_strategy()
{
    XRAdaptivePolicy p{};
    p.explorationPercent = 100;
    p.rollbackFailureStreak = 3;
    p.quarantineMs = 600000;
    XRAdaptiveIntelligence ai(p);

    const auto ctx = goodWifiContext();
    const auto caps = wifiOnlyCapabilities(true);

    XRAdaptiveDecision d = ai.choose(ctx, caps, 1000, 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(XRAdaptiveAction::WIFI_MQTT_PREFERRED), static_cast<uint8_t>(d.action));

    XRAdaptiveOutcome failed{};
    failed.transportFailed = true;
    for (uint8_t i = 0; i < 3; ++i)
        ai.learn(d, failed, 2000 + i);

    const auto &state = ai.state(d.contextBucket, XRAdaptiveAction::WIFI_MQTT_PREFERRED);
    TEST_ASSERT_EQUAL_UINT8(3, state.failureStreak);
    TEST_ASSERT_TRUE(state.quarantineUntilMs > 2000);

    const auto after = ai.choose(ctx, caps, 3000, 2);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(XRAdaptiveAction::BASELINE), static_cast<uint8_t>(after.action));
}

void test_proven_better_strategy_is_promoted_over_baseline()
{
    XRAdaptivePolicy p{};
    p.explorationPercent = 0;
    p.minimumSamplesForPromotion = 8;
    p.minimumRewardImprovement = 8;
    XRAdaptiveIntelligence ai(p);

    const auto ctx = goodWifiContext();
    const auto caps = wifiOnlyCapabilities(true);
    const auto baselineChoice = ai.choose(ctx, caps, 1000, 0);

    XRAdaptiveOutcome poor{};
    poor.delivered = false;
    for (uint8_t i = 0; i < 8; ++i) {
        XRAdaptiveDecision d{};
        d.action = XRAdaptiveAction::BASELINE;
        d.contextBucket = baselineChoice.contextBucket;
        ai.learn(d, poor, 2000 + i);
    }

    XRAdaptiveOutcome excellent{};
    excellent.delivered = true;
    excellent.acked = true;
    excellent.latencyMs = 250;
    for (uint8_t i = 0; i < 8; ++i) {
        XRAdaptiveDecision d{};
        d.action = XRAdaptiveAction::WIFI_MQTT_PREFERRED;
        d.contextBucket = baselineChoice.contextBucket;
        ai.learn(d, excellent, 3000 + i);
    }

    const auto promoted = ai.choose(ctx, caps, 5000, 0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(XRAdaptiveAction::WIFI_MQTT_PREFERRED),
                            static_cast<uint8_t>(promoted.action));
    TEST_ASSERT_TRUE(promoted.promoted);
}

void test_stale_promoted_strategy_returns_to_baseline_until_reproven()
{
    XRAdaptivePolicy p{};
    p.explorationPercent = 0;
    p.minimumSamplesForPromotion = 2;
    p.minimumRewardImprovement = 5;
    p.knowledgeFreshMs = 100;
    XRAdaptiveIntelligence ai(p);

    const auto ctx = goodWifiContext();
    const auto caps = wifiOnlyCapabilities(true);
    const auto initial = ai.choose(ctx, caps, 1000, 0);

    XRAdaptiveOutcome poor{};
    poor.delivered = false;
    XRAdaptiveOutcome excellent{};
    excellent.delivered = true;
    excellent.acked = true;

    for (uint8_t i = 0; i < 2; ++i) {
        XRAdaptiveDecision baseline{};
        baseline.action = XRAdaptiveAction::BASELINE;
        baseline.contextBucket = initial.contextBucket;
        ai.learn(baseline, poor, 1010 + i);

        XRAdaptiveDecision wifi{};
        wifi.action = XRAdaptiveAction::WIFI_MQTT_PREFERRED;
        wifi.contextBucket = initial.contextBucket;
        ai.learn(wifi, excellent, 1020 + i);
    }

    const auto fresh = ai.choose(ctx, caps, 1050, 0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(XRAdaptiveAction::WIFI_MQTT_PREFERRED), static_cast<uint8_t>(fresh.action));

    const auto stale = ai.choose(ctx, caps, 2000, 0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(XRAdaptiveAction::BASELINE), static_cast<uint8_t>(stale.action));
    TEST_ASSERT_FALSE(stale.promoted);
}

void test_snapshot_roundtrip_restores_learning()
{
    XRAdaptiveIntelligence source;
    XRAdaptiveDecision d{};
    d.action = XRAdaptiveAction::BASELINE;
    d.contextBucket = 0;

    XRAdaptiveOutcome outcome{};
    outcome.delivered = true;
    outcome.acked = true;
    source.learn(d, outcome, 1000);

    const auto snap = source.snapshot();
    XRAdaptiveIntelligence restored;
    TEST_ASSERT_TRUE(restored.restore(snap));
    TEST_ASSERT_EQUAL_UINT32(source.modelEpoch(), restored.modelEpoch());
    TEST_ASSERT_EQUAL_UINT16(source.state(0, XRAdaptiveAction::BASELINE).samples,
                             restored.state(0, XRAdaptiveAction::BASELINE).samples);
}

void test_corrupt_snapshot_is_rejected()
{
    XRAdaptiveIntelligence source;
    const auto clean = source.snapshot();
    auto corrupt = clean;
    corrupt.modelEpoch ^= 0x100u;

    XRAdaptiveIntelligence restored;
    TEST_ASSERT_FALSE(restored.restore(corrupt));
}

void low_battery_blocks_wifi_exploration_test()
{
    XRAdaptivePolicy p{};
    p.explorationPercent = 100;
    p.minimumBatteryForWifi = 30;
    XRAdaptiveIntelligence ai(p);

    auto ctx = goodWifiContext();
    ctx.batteryPercent = 20;
    const auto d = ai.choose(ctx, wifiOnlyCapabilities(true), 1000, 7);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(XRAdaptiveAction::BASELINE), static_cast<uint8_t>(d.action));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_private_wifi_is_never_selected_without_privacy_approval);
    RUN_TEST(test_success_reward_is_higher_than_failure_reward);
    RUN_TEST(test_privacy_rejection_is_absolute_negative_reward);
    RUN_TEST(test_three_failures_quarantine_nonbaseline_strategy);
    RUN_TEST(test_proven_better_strategy_is_promoted_over_baseline);
    RUN_TEST(test_stale_promoted_strategy_returns_to_baseline_until_reproven);
    RUN_TEST(test_snapshot_roundtrip_restores_learning);
    RUN_TEST(test_corrupt_snapshot_is_rejected);
    RUN_TEST(low_battery_blocks_wifi_exploration_test);
    std::exit(UNITY_END());
}

void loop() {}