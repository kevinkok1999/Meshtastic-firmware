#include "mesh/multilink/AdaptiveLinkManager.h"
#include "mesh/multilink/BackscatterLink.h"
#include <unity.h>

using namespace meshtastic::multilink;

namespace {

class FakeBackscatterFrontEnd final : public BackscatterFrontEnd
{
  public:
    bool begin(const BackscatterConfig &) override
    {
        beginCalls++;
        return beginOk;
    }
    bool available() const override { return isAvailable; }
    bool carrierPresent() const override { return carrier; }
    int16_t estimatedRssiDbm() const override { return rssi; }
    bool modulate(const uint8_t *data, size_t size, uint16_t symbolRate) override
    {
        ++modulateCalls;
        lastSize = size;
        lastSymbolRate = symbolRate;
        firstByte = size != 0 ? data[0] : 0;
        return acceptModulation;
    }
    void poll(uint32_t nowMs) override { lastPollMs = nowMs; }

    bool beginOk = true;
    bool isAvailable = true;
    bool carrier = true;
    bool acceptModulation = true;
    int16_t rssi = -48;
    uint32_t beginCalls = 0;
    uint32_t modulateCalls = 0;
    uint32_t lastPollMs = 0;
    size_t lastSize = 0;
    uint16_t lastSymbolRate = 0;
    uint8_t firstByte = 0;
};

FrameView makeFrame(TrafficClass trafficClass = TrafficClass::Telemetry, size_t size = 4)
{
    static uint8_t payload[64] = {0xBC, 1, 2, 3};
    FrameView frame;
    frame.data = payload;
    frame.size = size;
    frame.messageId = 33;
    frame.from = 1;
    frame.to = 2;
    frame.trafficClass = trafficClass;
    frame.requireEncryption = true;
    return frame;
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_disabled_mode_does_not_start()
{
    FakeBackscatterFrontEnd frontEnd;
    BackscatterConfig cfg;
    cfg.mode = BackscatterMode::Disabled;
    BackscatterLink link(frontEnd, cfg);

    TEST_ASSERT_FALSE(link.begin());
    TEST_ASSERT_EQUAL_UINT32(0, frontEnd.beginCalls);
    TEST_ASSERT_FALSE(link.metrics().available);
}

void test_carrier_gates_reachability_and_send()
{
    FakeBackscatterFrontEnd frontEnd;
    BackscatterConfig cfg;
    cfg.mode = BackscatterMode::Ambient;
    BackscatterLink link(frontEnd, cfg);
    TEST_ASSERT_TRUE(link.begin());

    frontEnd.carrier = false;
    TEST_ASSERT_FALSE(link.metrics().peerReachable);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SendResult::Unavailable), static_cast<uint8_t>(link.send(makeFrame())));
    TEST_ASSERT_EQUAL_UINT32(0, frontEnd.modulateCalls);

    frontEnd.carrier = true;
    TEST_ASSERT_TRUE(link.metrics().peerReachable);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SendResult::Accepted), static_cast<uint8_t>(link.send(makeFrame())));
    TEST_ASSERT_EQUAL_UINT32(1, frontEnd.modulateCalls);
    TEST_ASSERT_EQUAL_HEX8(0xBC, frontEnd.firstByte);
}

void test_bulk_is_rejected_until_frontend_is_validated_for_it()
{
    FakeBackscatterFrontEnd frontEnd;
    BackscatterConfig cfg;
    cfg.mode = BackscatterMode::ExternalTag;
    BackscatterLink link(frontEnd, cfg);
    TEST_ASSERT_TRUE(link.begin());

    TEST_ASSERT_FALSE(link.supports(makeFrame(TrafficClass::Bulk)));
}

void test_mtu_is_enforced()
{
    FakeBackscatterFrontEnd frontEnd;
    BackscatterConfig cfg;
    cfg.mode = BackscatterMode::ReaderAssisted;
    cfg.mtu = 8;
    BackscatterLink link(frontEnd, cfg);
    TEST_ASSERT_TRUE(link.begin());

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SendResult::TooLarge), static_cast<uint8_t>(link.send(makeFrame(TrafficClass::Telemetry, 16))));
}

void test_telemetry_can_win_when_backscatter_is_healthy()
{
    FakeBackscatterFrontEnd frontEnd;
    BackscatterConfig cfg;
    cfg.mode = BackscatterMode::Ambient;
    cfg.mtu = 32;
    BackscatterLink backscatter(frontEnd, cfg);
    TEST_ASSERT_TRUE(backscatter.begin());
    backscatter.poll(100);
    backscatter.reportDelivery(true, 10);

    AdaptiveLinkManager manager;
    TEST_ASSERT_TRUE(manager.addLink(backscatter));
    const SendPlan plan = manager.select(makeFrame(TrafficClass::Telemetry), 101);

    TEST_ASSERT_EQUAL_UINT8(1, plan.count);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkType::Backscatter), static_cast<uint8_t>(plan.links[0]->type()));
}

void setup()
{
    UNITY_BEGIN();
    RUN_TEST(test_disabled_mode_does_not_start);
    RUN_TEST(test_carrier_gates_reachability_and_send);
    RUN_TEST(test_bulk_is_rejected_until_frontend_is_validated_for_it);
    RUN_TEST(test_mtu_is_enforced);
    RUN_TEST(test_telemetry_can_win_when_backscatter_is_healthy);
    UNITY_END();
}

void loop() {}
