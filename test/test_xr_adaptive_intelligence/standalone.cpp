#include "xr/XRAdaptiveIntelligence.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

using namespace meshoffgrid::xr;

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "XRAdaptiveIntelligence regression failure: " << message << "\n";
        std::exit(1);
    }
}

XRAdaptiveContext goodWifiContext()
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

XRAdaptiveCapabilities wifiOnlyCapabilities(bool privacyApproved = true)
{
    XRAdaptiveCapabilities caps{};
    caps.loraAvailable = false;
    caps.wifiMqttAvailable = true;
    caps.wifiMqttPrivacyApproved = privacyApproved;
    return caps;
}

void privacyGateTest()
{
    XRAdaptivePolicy policy{};
    policy.explorationPercent = 100;
    XRAdaptiveIntelligence ai(policy);

    const auto decision = ai.choose(goodWifiContext(), wifiOnlyCapabilities(false), 1000, 42);
    require(decision.action != XRAdaptiveAction::WIFI_MQTT_PREFERRED,
            "privacy-unapproved Wi-Fi must never be selected");

    XRAdaptiveOutcome rejected{};
    rejected.delivered = true;
    rejected.acked = true;
    rejected.privacyRejected = true;
    require(XRAdaptiveIntelligence::scoreOutcome(rejected) == -100,
            "privacy rejection must remain an absolute negative reward");
}

void endToEndAuthorityTest()
{
    XRAdaptiveOutcome carrierOnly{};
    carrierOnly.transportAccepted = true;

    XRAdaptiveOutcome delivered{};
    delivered.delivered = true;
    delivered.acked = true;

    require(XRAdaptiveIntelligence::scoreOutcome(delivered) >
                XRAdaptiveIntelligence::scoreOutcome(carrierOnly),
            "carrier acceptance must stay weaker evidence than end-to-end delivery");
}

void lowBatteryGateTest()
{
    XRAdaptivePolicy policy{};
    policy.explorationPercent = 100;
    policy.minimumBatteryForWifi = 30;
    XRAdaptiveIntelligence ai(policy);

    auto context = goodWifiContext();
    context.batteryPercent = 20;
    const auto decision = ai.choose(context, wifiOnlyCapabilities(true), 1000, 7);
    require(decision.action != XRAdaptiveAction::WIFI_MQTT_PREFERRED,
            "low battery must block Wi-Fi exploration");
}

void quarantineRollbackTest()
{
    XRAdaptivePolicy policy{};
    policy.explorationPercent = 100;
    policy.rollbackFailureStreak = 3;
    policy.quarantineMs = 600000;
    XRAdaptiveIntelligence ai(policy);

    const auto context = goodWifiContext();
    const auto capabilities = wifiOnlyCapabilities(true);
    const auto decision = ai.choose(context, capabilities, 1000, 1);
    require(decision.action == XRAdaptiveAction::WIFI_MQTT_PREFERRED,
            "healthy privacy-approved Wi-Fi should be explorable");

    XRAdaptiveOutcome failed{};
    failed.transportFailed = true;
    for (uint8_t i = 0; i < 3; ++i)
        ai.learn(decision, failed, 2000 + i);

    const auto &state = ai.state(decision.contextBucket, XRAdaptiveAction::WIFI_MQTT_PREFERRED);
    require(state.failureStreak == 3, "three consecutive failures must be recorded");
    require(state.quarantineUntilMs > 2000, "failed strategy must enter quarantine");

    const auto after = ai.choose(context, capabilities, 3000, 2);
    require(after.action != XRAdaptiveAction::WIFI_MQTT_PREFERRED,
            "quarantined strategy must not be selected during rollback");
}

void promotionAndFreshnessTest()
{
    XRAdaptivePolicy policy{};
    policy.explorationPercent = 0;
    policy.minimumSamplesForPromotion = 4;
    policy.minimumRewardImprovement = 8;
    policy.knowledgeFreshMs = 100;
    XRAdaptiveIntelligence ai(policy);

    const auto context = goodWifiContext();
    const auto capabilities = wifiOnlyCapabilities(true);
    const auto initial = ai.choose(context, capabilities, 1000, 0);

    XRAdaptiveOutcome poor{};
    poor.delivered = false;

    XRAdaptiveOutcome excellent{};
    excellent.delivered = true;
    excellent.acked = true;
    excellent.latencyMs = 250;

    for (uint8_t i = 0; i < 4; ++i) {
        XRAdaptiveDecision baseline{};
        baseline.action = XRAdaptiveAction::BASELINE;
        baseline.contextBucket = initial.contextBucket;
        ai.learn(baseline, poor, 1010 + i);

        XRAdaptiveDecision wifi{};
        wifi.action = XRAdaptiveAction::WIFI_MQTT_PREFERRED;
        wifi.contextBucket = initial.contextBucket;
        ai.learn(wifi, excellent, 1020 + i);
    }

    const auto promoted = ai.choose(context, capabilities, 1050, 0);
    require(promoted.action == XRAdaptiveAction::WIFI_MQTT_PREFERRED && promoted.promoted,
            "fresh, proven-better strategy must be promoted");

    const auto stale = ai.choose(context, capabilities, 2000, 0);
    require(stale.action == XRAdaptiveAction::BASELINE && !stale.promoted,
            "stale learned strategy must return to baseline until reproven");
}

void persistenceIntegrityTest()
{
    XRAdaptivePolicy policy{};
    policy.minimumPersistIntervalMs = 3600000;
    XRAdaptiveIntelligence source(policy);

    XRAdaptiveDecision decision{};
    decision.action = XRAdaptiveAction::BASELINE;
    decision.contextBucket = 0;

    XRAdaptiveOutcome outcome{};
    outcome.delivered = true;
    outcome.acked = true;
    source.learn(decision, outcome, 1000);

    require(source.shouldPersist(1000), "new learning must become persistable");
    source.markPersisted(1000);
    require(!source.shouldPersist(1001), "clean model must not rewrite flash");

    source.learn(decision, outcome, 2000);
    require(!source.shouldPersist(2000), "flash persistence must remain rate-limited");
    require(source.shouldPersist(3601000), "dirty model must persist after the interval");

    const auto clean = source.snapshot();
    XRAdaptiveIntelligence restored(policy);
    require(restored.restore(clean), "valid snapshot must restore");
    require(restored.modelEpoch() == source.modelEpoch(), "model epoch must survive restore");

    auto corrupt = clean;
    corrupt.modelEpoch ^= 0x100u;
    XRAdaptiveIntelligence rejected(policy);
    require(!rejected.restore(corrupt), "corrupted snapshot must be rejected");
}

} // namespace

int main()
{
    privacyGateTest();
    endToEndAuthorityTest();
    lowBatteryGateTest();
    quarantineRollbackTest();
    promotionAndFreshnessTest();
    persistenceIntegrityTest();

    std::cout << "XRAdaptiveIntelligence standalone regression tests PASSED\n";
    return 0;
}
