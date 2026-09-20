#include "V9ReachEngine.h"
#include "V9DeliveryCore.h"
#include "V9StoreForward.h"

#include <cassert>
#include <cstring>

using namespace ops;
using namespace ops::v9;

int main() {
    V9ReachEngine reach;
    reach.setAvailable(V9Route::LoRa, true, 100);
    reach.setReachable(V9Route::LoRa, true, 100);
    reach.setAvailable(V9Route::EspNowLR, true, 100);
    reach.setReachable(V9Route::EspNowLR, true, 100);

    auto d = reach.decide(5000);
    assert(d.hasPrimary);
    assert(d.primary == V9Route::EspNowLR || d.primary == V9Route::LoRa);

    reach.noteFailure(V9Route::EspNowLR, 6000);
    reach.noteFailure(V9Route::EspNowLR, 6100);
    reach.noteFailure(V9Route::EspNowLR, 6200);
    d = reach.decide(10000);
    assert(d.primary == V9Route::LoRa);

    MessageIdGenerator ids;
    ids.seed(0x1122334455667788ULL, 0x8877665544332211ULL);
    const auto a = ids.next();
    const auto b = ids.next();
    assert(a != b);

    SelectiveAck32 ack;
    ack.markReceived(0);
    ack.markReceived(2);
    assert(!ack.complete(3));
    assert(ack.missingMask(3) == (1UL << 1));
    ack.markReceived(1);
    assert(ack.complete(3));

    uint8_t f0[4] = {1,2,3,4};
    uint8_t f1[4] = {5,6,7,8};
    uint8_t f2[4] = {9,10,11,12};
    const uint8_t* src[3] = {f0,f1,f2};
    size_t lengths[3] = {4,4,4};
    uint8_t parity[8] = {};
    size_t parityLen = 0;
    assert(XorErasureFec::makeParity(src, lengths, 3, parity, sizeof(parity), parityLen));
    assert(parityLen == 4);

    uint8_t r0[4]; std::memcpy(r0, f0, 4);
    uint8_t r1[4] = {};
    uint8_t r2[4]; std::memcpy(r2, f2, 4);
    uint8_t* recovered[3] = {r0,r1,r2};
    size_t recoveredLengths[3] = {4,0,4};
    bool present[3] = {true,false,true};
    assert(XorErasureFec::recoverOne(recovered, recoveredLengths, present, 3, parity, parityLen, 1, 4));
    assert(std::memcmp(r1, f1, 4) == 0);

    V9StoreForward store;
    const uint8_t dest[4] = {1,2,3,4};
    assert(store.enqueue(a, dest, "delayed message", 1000, 60000));
    assert(store.count() == 1);
    assert(store.nextDue(2500) < 0);
    const int idx = store.nextDue(3001);
    assert(idx >= 0);
    store.markFailed(static_cast<size_t>(idx), 3001);
    assert(store.count() == 1);
    const auto* item = store.item(static_cast<size_t>(idx));
    assert(item && item->attempts == 1);
    store.markAccepted(static_cast<size_t>(idx));
    assert(store.count() == 0);

    return 0;
}
