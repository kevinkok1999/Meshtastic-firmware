#include "mesh/xbee/XBeeApiCodec.h"
#include "xr/XRRfCoexistence.h"

#include <array>
#include <cassert>
#include <cstdint>

using meshoffgrid::xbee::API_START_DELIMITER;
using meshoffgrid::xbee::FRAME_AT_COMMAND;
using meshoffgrid::xbee::FRAME_TX_REQUEST;
using meshoffgrid::xbee::XBeeApiCodec;
using meshoffgrid::xr::XRRfCoexistence;

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
    testRfCoexistenceGuard();
    return 0;
}
