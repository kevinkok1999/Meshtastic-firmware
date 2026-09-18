#include "mesh/multilink/AdaptiveLinkManager.h"
#include "mesh/multilink/EspNowLink.h"
#include <unity.h>

using namespace meshtastic::multilink;

namespace {

class FakeEspNowBackend final : public EspNowBackend
{
  public:
    bool begin(const EspNowConfig &) override { return up; }
    bool available() const override { return up; }
    bool hasPeer(uint32_t nodeId) const override { return nodeId == knownPeer; }
    bool send(uint32_t nodeId, const uint8_t *, size_t) override
    {
        lastSentPeer = nodeId;
        ++sendCount;
        return sendAccepted;
    }
    int16_t peerRssiDbm(uint32_t) const override { return rssi; }
    bool popDeliveryReport(EspNowDeliveryReport &out) override
    {
        if (!haveReport)
            return false;
        out = report;
        haveReport = false;
        return true;
    }
    bool popReceived(EspNowRxFrame &out) override
    {
        if (!haveRx)
            return false;
        out = rx;
        haveRx = false;
        return true;
    }
    void poll(uint32_t) override {}

    bool up = true;
    bool sendAccepted = true;
    bool haveReport = false;
    bool haveRx = false;
    uint32_t knownPeer = 42;
    uint32_t lastSentPeer = 0;
    uint32_t sendCount = 0;
    int16_t rssi = -55;
    EspNowDeliveryReport report{};
    EspNowRxFrame rx{};
};

class CaptureSink final : public LinkReceiveSink
{
  public:
    void onLinkFrame(const ReceivedFrameView &frame) override
    {
        ++calls;
        from = frame.from;
        size = frame.size;
        firstByte = frame.size != 0 ? frame.data[0] : 0;
        authenticated = frame.transportAuthenticated;
    }

    uint32_t calls = 0;
    uint32_t from = 0;
    size_t size = 0;
    uint8_t firstByte = 0;
    bool authenticated = false;
};

FrameView makeFrame(uint32_t to = 42)
{
    static uint8_t payload[] = {1, 2, 3};
    FrameView frame;
    frame.data = payload;
    frame.size = sizeof(payload);
    frame.messageId = 10;
    frame.from = 7;
    frame.to = to;
    frame.trafficClass = TrafficClass::Interactive;
    frame.requireAck = true;
    frame.requireEncryption = true;
    return frame;
}

} // namespace

void setUp(void) {}
void tearDown(void) {}

void test_first_packet_is_selectable_before_last_peer_exists()
{
    FakeEspNowBackend backend;
    EspNowLink espnow(backend);
    TEST_ASSERT_TRUE(espnow.begin());

    AdaptiveLinkManager manager;
    TEST_ASSERT_TRUE(manager.addLink(espnow));

    const SendPlan plan = manager.select(makeFrame(), 100);
    TEST_ASSERT_EQUAL_UINT8(1, plan.count);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LinkType::EspNow), static_cast<uint8_t>(plan.links[0]->type()));
}

void test_unknown_peer_is_rejected()
{
    FakeEspNowBackend backend;
    EspNowLink espnow(backend);
    TEST_ASSERT_TRUE(espnow.begin());
    TEST_ASSERT_FALSE(espnow.supports(makeFrame(99)));
}

void test_send_targets_registered_peer()
{
    FakeEspNowBackend backend;
    EspNowLink espnow(backend);
    TEST_ASSERT_TRUE(espnow.begin());

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SendResult::Accepted), static_cast<uint8_t>(espnow.send(makeFrame())));
    TEST_ASSERT_EQUAL_UINT32(42, backend.lastSentPeer);
    TEST_ASSERT_EQUAL_UINT32(1, backend.sendCount);
}

void test_receive_is_deferred_to_poll_sink()
{
    FakeEspNowBackend backend;
    EspNowLink espnow(backend);
    CaptureSink sink;
    espnow.setReceiveSink(&sink);
    TEST_ASSERT_TRUE(espnow.begin());

    backend.rx.size = 2;
    backend.rx.nodeId = 42;
    backend.rx.rssiDbm = -61;
    backend.rx.transportAuthenticated = true;
    backend.rx.data[0] = 0xA5;
    backend.rx.data[1] = 0x5A;
    backend.haveRx = true;

    TEST_ASSERT_EQUAL_UINT32(0, sink.calls);
    espnow.poll(250);

    TEST_ASSERT_EQUAL_UINT32(1, sink.calls);
    TEST_ASSERT_EQUAL_UINT32(42, sink.from);
    TEST_ASSERT_EQUAL_UINT32(2, sink.size);
    TEST_ASSERT_EQUAL_HEX8(0xA5, sink.firstByte);
    TEST_ASSERT_TRUE(sink.authenticated);
}

void test_delivery_callback_updates_metrics()
{
    FakeEspNowBackend backend;
    EspNowLink espnow(backend);
    TEST_ASSERT_TRUE(espnow.begin());

    backend.report.nodeId = 42;
    backend.report.success = true;
    backend.report.latencyMs = 11;
    backend.haveReport = true;
    espnow.poll(300);

    const LinkMetrics metrics = espnow.metrics();
    TEST_ASSERT_EQUAL_UINT16(11, metrics.latencyMs);
    TEST_ASSERT_EQUAL_INT16(-55, metrics.rssiDbm);
}

void setup()
{
    UNITY_BEGIN();
    RUN_TEST(test_first_packet_is_selectable_before_last_peer_exists);
    RUN_TEST(test_unknown_peer_is_rejected);
    RUN_TEST(test_send_targets_registered_peer);
    RUN_TEST(test_receive_is_deferred_to_poll_sink);
    RUN_TEST(test_delivery_callback_updates_metrics);
    UNITY_END();
}

void loop() {}
