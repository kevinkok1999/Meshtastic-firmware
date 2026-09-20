#include "V10DirectLinkCore.h"

#include <cassert>

using namespace ops::v10;

int main() {
    DirectLinkBrain brain;
    brain.setAvailable(BEARER_LORA, true, 100);
    brain.setReachable(BEARER_LORA, true, 100);
    brain.setAvailable(BEARER_ESPNOW_LR, true, 100);
    brain.setReachable(BEARER_ESPNOW_LR, true, 100);

    Decision d = brain.decide(1000);
    assert(d.hasPrimary);

    brain.setAvailable(BEARER_WIFI_LR, true, 2000);
    brain.setReachable(BEARER_WIFI_LR, true, 2000);
    brain.noteSuccess(BEARER_WIFI_LR, 80, 2100);
    brain.noteSuccess(BEARER_WIFI_LR, 75, 2200);
    d = brain.decide(5000);
    assert(d.hasPrimary);
    assert(d.primary == BEARER_WIFI_LR || d.primary == BEARER_ESPNOW_LR || d.primary == BEARER_LORA);

    brain.noteFailure(BEARER_WIFI_LR, 6000);
    brain.noteFailure(BEARER_WIFI_LR, 6100);
    brain.noteFailure(BEARER_WIFI_LR, 6200);
    d = brain.decide(10000);
    assert(d.primary != BEARER_WIFI_LR);

    ResourceScheduler scheduler;
    assert(scheduler.acquire(RESOURCE_RF24, BEARER_WIFI_LR, 100, 1000));
    assert(!scheduler.acquire(RESOURCE_RF24, BEARER_ESPNOW_LR, 100, 1000));
    scheduler.release(RESOURCE_RF24, BEARER_WIFI_LR);
    assert(scheduler.acquire(RESOURCE_RF24, BEARER_ESPNOW_LR, 200, 1000));

    assert(DirectLinkBrain::resource(BEARER_LORA) == RESOURCE_SUBGHZ);
    assert(DirectLinkBrain::resource(BEARER_GFSK) == RESOURCE_SUBGHZ);
    assert(DirectLinkBrain::resource(BEARER_WIFI_LR) == RESOURCE_RF24);
    assert(DirectLinkBrain::resource(BEARER_BLE_CODED) == RESOURCE_RF24);

    return 0;
}
