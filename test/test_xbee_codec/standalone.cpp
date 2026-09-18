#include "mesh/xbee/XBeeApiCodec.h"
#include "xr/XRRfCoexistence.h"
#include "xr/XRDeliveryEvents.h"

#include <array>
#include <cassert>
#include <cstdint>

using meshoffgrid::xbee::API_START_DELIMITER;
using meshoffgrid::xbee::FRAME_AT_COMMAND;
using meshoffgrid::xbee::FRAME_TX_REQUEST;
using meshoffgrid::xbee::XBeeApiCodec;
using meshoffgrid::xr::XRRfCoexistence;
using meshoffgrid::xr::XRDeliveryEventSink;
using meshoffgrid::xr::XRDeliveryEvents;

static void testTransmitFrame()
{
    std::array<uint8_t, 64> out{};
    const uint8_t payload[] = {0x11, 0x22, 0x33};
    const size_t n = XBeeApiCodec::buildTransmitRequest(out.data(), out.size(), 0x52, 0x0013A20040B41234ULL,
                                                        payload, sizeof(payload));
    assert(n == 21);
    assert(out[0] == API_START_DELIMITER);
    assert(out[3] == FRAME_TX_REQUEST);
    assert(out[4] == 0x52);

    const uint16_t frameDataLength = static_cast<uint16_t>((out[1] << 8) | out[2]);
    assert(frameDataLength == 17);
    assert(XBeeApiCodec::checksumValid(&out[3], frameDataLength, out[3 + frameDataLength]));
}

static void testAtFrame()
{
    std::array<uint8_t, 32> out{};
    const char command[2] = {'N', 'P'};
    const size_t n = XBeeApiCodec::buildAtCommand(out.data(), out.size(), 7, command);
    assert(n == 8);
    assert(out[0] == API_START_DELIMITER);
    assert(out[3] == FRAME_AT_COMMAND);
    assert(out[4] == 7);
    assert(out[5] == 'N');
    assert(out[6] == 'P');
    assert(XBeeApiCodec::checksumValid(&out[3], 4, out[7]));
}

static void testBufferRejection()
{
    std::array<uint8_t, 4> tiny{};
    const uint8_t payload[] = {1, 2, 3};
    assert(XBeeApiCodec::buildTransmitRequest(tiny.data(), tiny.size(), 1, 1, payload, sizeof(payload)) == 0);
}

class TestDeliverySink final : public XRDeliveryEventSink
{
  public:
    uint32_t failed = 0;
    uint32_t acked = 0;
    uint32_t naked = 0;

    void onReliableDeliveryFailed(uint32_t, uint32_t, uint32_t) override { ++failed; }
    void onReliableDeliveryAcked(uint32_t, uint32_t, uint32_t) override { ++acked; }
    void onReliableDeliveryNaked(uint32_t, uint32_t, uint32_t) override { ++naked; }
};

static void testDeliveryEvents()
{
    TestDeliverySink sink;
    assert(XRDeliveryEvents::addSink(&sink));

    XRDeliveryEvents::notifyFailed(1, 2, 3);
    XRDeliveryEvents::notifyAcked(1, 2, 4);
    XRDeliveryEvents::notifyNaked(1, 2, 5);

    assert(sink.failed == 1);
    assert(sink.acked == 1);
    assert(sink.naked == 1);

    XRDeliveryEvents::removeSink(&sink);
    XRDeliveryEvents::notifyFailed(1, 2, 6);
    assert(sink.failed == 1);
}

static void testRfCoexistenceGuard()
{
    XRRfCoexistence guard;
    assert(guard.canUseSecondary(1000));

    guard.onLoRaTxStart(1000);
    assert(!guard.canUseSecondary(1001));

    guard.onLoRaTxEnd(1100, false);
    assert(!guard.canUseSecondary(1300));
    assert(guard.canUseSecondary(1450));

    guard.onLoRaTxStart(2000);
    guard.onLoRaTxEnd(2100, true);
    assert(!guard.canUseSecondary(3000));
    assert(guard.canUseSecondary(3500));
}

int main()
{
    testTransmitFrame();
    testAtFrame();
    testBufferRejection();
    testDeliveryEvents();
    testRfCoexistenceGuard();
    return 0;
}
