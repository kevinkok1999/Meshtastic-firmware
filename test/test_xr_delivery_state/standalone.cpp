#include "xr/XRDeliveryStateMachine.h"

#include <cassert>

using namespace meshoffgrid::xr;

static void carrier_success_is_not_delivery()
{
    XRDeliveryStateMachine sm;
    assert(sm.begin(0x20, 1, 1000));
    assert(sm.startSecondary(0x20, 1, XRDeliveryPath::EspNow, 1100));
    assert(sm.markCarrierResult(0x20, 1, XRDeliveryPath::EspNow, true, 1200));

    const auto *entry = sm.find(0x20, 1);
    assert(entry != nullptr);
    assert(entry->phase == XRDeliveryPhase::CarrierAccepted);
    assert(entry->phase != XRDeliveryPhase::Delivered);
    assert(!sm.canStartSecondary(0x20, 1, XRDeliveryPath::XBee));
}

static void only_authoritative_ack_finishes_delivery()
{
    XRDeliveryStateMachine sm;
    assert(sm.begin(0x21, 2, 1000));
    assert(sm.markPrimaryFailed(0x21, 2, 2000));
    assert(sm.startSecondary(0x21, 2, XRDeliveryPath::XBee, 2100));
    assert(sm.markCarrierResult(0x21, 2, XRDeliveryPath::XBee, true, 2200));
    assert(sm.markDelivered(0x21, 2, 2600));

    const auto *entry = sm.find(0x21, 2);
    assert(entry && entry->phase == XRDeliveryPhase::Delivered);
    assert(!sm.startSecondary(0x21, 2, XRDeliveryPath::EspNow, 2700));
}

static void failed_secondary_returns_to_recovery()
{
    XRDeliveryStateMachine sm;
    assert(sm.begin(0x22, 3, 1000));
    assert(sm.markPrimaryFailed(0x22, 3, 1500));
    assert(sm.startSecondary(0x22, 3, XRDeliveryPath::EspNow, 1600));
    assert(sm.markCarrierResult(0x22, 3, XRDeliveryPath::EspNow, false, 1700));

    const auto *entry = sm.find(0x22, 3);
    assert(entry && entry->phase == XRDeliveryPhase::RecoveryQueued);
    assert(sm.canStartSecondary(0x22, 3, XRDeliveryPath::XBee));
}

static void second_sidecar_cannot_race_active_one()
{
    XRDeliveryStateMachine sm;
    assert(sm.begin(0x23, 4, 1000));
    assert(sm.startSecondary(0x23, 4, XRDeliveryPath::EspNow, 1100));
    assert(!sm.startSecondary(0x23, 4, XRDeliveryPath::XBee, 1101));
}


static void duplicated_primary_failure_is_idempotent()
{
    XRDeliveryStateMachine sm;
    assert(sm.begin(0x25, 6, 1000));
    assert(sm.markPrimaryFailed(0x25, 6, 1500));
    assert(sm.markPrimaryFailed(0x25, 6, 1501));

    const auto *entry = sm.find(0x25, 6);
    assert(entry != nullptr);
    assert(entry->phase == XRDeliveryPhase::RecoveryQueued);
    assert(entry->primaryFailures == 1);
}

static void expiry_is_terminal()
{
    XRDeliveryStateMachine sm;
    assert(sm.begin(0x24, 5, 1000));
    sm.expire(2001, 1000);

    const auto *entry = sm.find(0x24, 5);
    assert(entry && entry->phase == XRDeliveryPhase::Expired);
    assert(!sm.startSecondary(0x24, 5, XRDeliveryPath::XBee, 2100));
}

int main()
{
    carrier_success_is_not_delivery();
    only_authoritative_ack_finishes_delivery();
    failed_secondary_returns_to_recovery();
    second_sidecar_cannot_race_active_one();
    duplicated_primary_failure_is_idempotent();
    expiry_is_terminal();
    return 0;
}
