#include "V8RouteCore.h"
#include "XBeeApiCodec.h"

#include <cassert>
#include <cstdint>

using namespace ops;

int main() {
    V8RouteCore routes;
    routes.setAvailable(V8Route::LoRa, true, 100);
    routes.setReachable(V8Route::LoRa, true, 100);
    routes.setAvailable(V8Route::EspNowLR, true, 100);
    routes.setReachable(V8Route::EspNowLR, false, 100);
    assert(routes.choose(100) == V8Route::LoRa);

    routes.setReachable(V8Route::EspNowLR, true, 200);
    assert(routes.choose(3000) == V8Route::EspNowLR);

    routes.setMode(V8RouteMode::RangeFirst);
    routes.setAvailable(V8Route::XBeeXR868, true, 4000);
    routes.setReachable(V8Route::XBeeXR868, true, 4000);
    for (int i = 0; i < 3; ++i) routes.noteSuccess(V8Route::XBeeXR868, 200, 4100 + i);
    assert(routes.choose(7000) == V8Route::XBeeXR868);

    uint8_t frame[128] = {};
    const uint8_t payload[] = {1,2,3,4,5};
    const size_t n = ops::v8::XBeeApiCodec::buildTransmitRequest(
        frame, sizeof(frame), 1, 0x0013A20012345678ULL, payload, sizeof(payload));
    assert(n > 0);
    const size_t frameDataLength = (static_cast<size_t>(frame[1]) << 8) | frame[2];
    assert(ops::v8::XBeeApiCodec::checksumValid(&frame[3], frameDataLength, frame[3 + frameDataLength]));
    return 0;
}
