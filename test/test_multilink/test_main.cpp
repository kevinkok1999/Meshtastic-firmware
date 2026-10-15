#include "mesh/multilink/AdaptiveLinkManager.h"
#include <unity.h>

using namespace meshtastic::multilink;

namespace {

class FakeLink final : public LinkTransport
{
  public:
    FakeLink(LinkType type, const char *name, LinkMetrics metrics) : type_(type), name_(name), metrics_(metrics) {}

    LinkType type() const override { return type_; }
    const char *name() const override { return name_; }
    LinkMetrics metrics() const override { return metrics_; }
    bool supports(const FrameView &frame) const override { return frame.size <= metrics_.mtu; }
    SendResult send(const FrameView &) override
    {
        ++sendCount;
        return sendResult;
    }
    void poll(uint32_t) override {}

    LinkMetrics &mutableMetrics() { return metrics_; }

    uint32_t sendCount = 0;
    SendResult sendResult = SendResult::Accepted;

  private:
    LinkType type_;
    const char *name_;
    LinkMetrics metrics_;
};

LinkMetrics goodMetrics(int16_t rssi, uint16_t latency, uint16_t energy)
{
    LinkMetrics m;
    m.available = true;
    m.peerReachable = true;
    m.encrypted = true;
    m.rssiDbm = rssi;
    m.deliveryPermille = 900;
    m.latencyMs = latency;
    m.energyCost = energy;
    m.mtu = 240;
    m.lastUpdateMs = 100;
    return m;
}

FrameView frame(TrafficClass trafficClass = TrafficClass::Interactive)
{
    static uint8_t payload[16] = {};
    FrameView f;
    f.data = payload;
    f.size = sizeof(payload);
    f.messageId = 0x1234;
    f.from = 1;
    f.to = 2;
    f.trafficClass = trafficClass;
    f.requireAck = true;
    f.requireEncryption = true;
    return f;
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_interactive_prefers_fast_local_link()
{
    FakeLink lora(LinkType::LoRa, "lora", goodMetrics(-104, 650, 650));
    FakeLink espnow(LinkType::EspNow, "espnow", goodMetrics(-58, 18, 280));

    AdaptiveLinkManager manager;
    TEST_ASSERT_TRUE(manager.addLink(lora));
    TEST_ASSERT_TRUE(manager.addLink(espnow));

    const SendPlan plan = manager.select(frame(), 200);
    TEST_ASSERT_EQUAL_UINT8(1, plan.count);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkType::EspNow), static_cast<uint8_t>(plan.links[0]->type()));
}

void test_failover_ignores_unreachable_link()
{
    FakeLink lora(LinkType::LoRa, "lora", goodMetrics(-108, 700, 650));
    FakeLink espnow(LinkType::EspNow, "espnow", goodMetrics(-50, 12, 250));
    espnow.mutableMetrics().peerReachable = false;

    AdaptiveLinkManager manager;
    manager.addLink(lora);
    manager.addLink(espnow);

    const SendPlan plan = manager.select(frame(), 200);
    TEST_ASSERT_EQUAL_UINT8(1, plan.count);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkType::LoRa), static_cast<uint8_t>(plan.links[0]->type()));
}

void test_hysteresis_prevents_link_flapping()
{
    SelectionPolicy policy;
    policy.hysteresisScore = 500;
    policy.stickyWindowMs = 5000;

    FakeLink lora(LinkType::LoRa, "lora", goodMetrics(-70, 80, 400));
    FakeLink espnow(LinkType::EspNow, "espnow", goodMetrics(-75, 120, 400));

    AdaptiveLinkManager manager(policy);
    manager.addLink(lora);
    manager.addLink(espnow);

    const SendPlan first = manager.select(frame(), 200);
    TEST_ASSERT_NOT_NULL(first.links[0]);
    const LinkType original = first.links[0]->type();

    // Make the other link only slightly better. Sticky hysteresis should keep
    // the current bearer rather than oscillating on every metrics update.
    if (original == LinkType::LoRa) {
        espnow.mutableMetrics().latencyMs = 1;
    } else {
        lora.mutableMetrics().latencyMs = 1;
    }

    const SendPlan second = manager.select(frame(), 400);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(original), static_cast<uint8_t>(second.links[0]->type()));
}

void test_control_can_duplicate_across_two_bearers()
{
    FakeLink lora(LinkType::LoRa, "lora", goodMetrics(-95, 400, 600));
    FakeLink espnow(LinkType::EspNow, "espnow", goodMetrics(-60, 20, 250));

    AdaptiveLinkManager manager;
    manager.addLink(lora);
    manager.addLink(espnow);

    const SendPlan plan = manager.select(frame(TrafficClass::Control), 200);
    TEST_ASSERT_EQUAL_UINT8(2, plan.count);
    TEST_ASSERT_NOT_EQUAL(plan.links[0], plan.links[1]);
}

void test_dedupe_detects_same_message_on_second_bearer()
{
    AdaptiveLinkManager manager;
    TEST_ASSERT_FALSE(manager.seenRecently(7, 42, 1000));
    manager.remember(7, 42, 1000);
    TEST_ASSERT_TRUE(manager.seenRecently(7, 42, 1001));
    TEST_ASSERT_FALSE(manager.seenRecently(7, 43, 1001));
    TEST_ASSERT_FALSE(manager.seenRecently(7, 42, 200000, 1000));
}

void test_encryption_policy_rejects_unprotected_bearer()
{
    FakeLink lora(LinkType::LoRa, "lora", goodMetrics(-100, 500, 600));
    FakeLink nan(LinkType::WifiNan, "nan", goodMetrics(-45, 8, 300));
    nan.mutableMetrics().encrypted = false;

    AdaptiveLinkManager manager;
    manager.addLink(lora);
    manager.addLink(nan);

    const SendPlan plan = manager.select(frame(TrafficClass::Bulk), 200);
    TEST_ASSERT_EQUAL_UINT8(1, plan.count);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkType::LoRa), static_cast<uint8_t>(plan.links[0]->type()));
}

void setup()
{
    UNITY_BEGIN();
    RUN_TEST(test_interactive_prefers_fast_local_link);
    RUN_TEST(test_failover_ignores_unreachable_link);
    RUN_TEST(test_hysteresis_prevents_link_flapping);
    RUN_TEST(test_control_can_duplicate_across_two_bearers);
    RUN_TEST(test_dedupe_detects_same_message_on_second_bearer);
    RUN_TEST(test_encryption_policy_rejects_unprotected_bearer);
    UNITY_END();
}

void loop() {}
