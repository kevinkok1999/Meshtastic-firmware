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

static void unacked_assist_does_not_block_recovery_after_lora_failure()
{
    XRDeliveryStateMachine sm;
    assert(sm.begin(0x26, 7, 1000));
    assert(sm.startSecondary(0x26, 7, XRDeliveryPath::EspNow, 1050));
    assert(sm.markCarrierResult(0x26, 7, XRDeliveryPath::EspNow, true, 1100));

    const auto *accepted = sm.find(0x26, 7);
    assert(accepted && accepted->phase == XRDeliveryPhase::CarrierAccepted);

    assert(sm.markPrimaryFailed(0x26, 7, 1500));
    const auto *recovery = sm.find(0x26, 7);
    assert(recovery && recovery->phase == XRDeliveryPhase::RecoveryQueued);
    assert(sm.canStartSecondary(0x26, 7, XRDeliveryPath::XBee));
}

static void primary_failure_waits_for_inflight_assist()
{
    XRDeliveryStateMachine sm;
    assert(sm.begin(0x27, 8, 1000));
    assert(sm.startSecondary(0x27, 8, XRDeliveryPath::EspNow, 1050));

    assert(sm.markPrimaryFailed(0x27, 8, 1060));
    const auto *inflight = sm.find(0x27, 8);
    assert(inflight != nullptr);
    assert(inflight->primaryFailed);
    assert(inflight->primaryFailures == 1);
    assert(inflight->phase == XRDeliveryPhase::SecondaryActive);
    assert(!sm.canStartSecondary(0x27, 8, XRDeliveryPath::XBee));

    assert(sm.markCarrierResult(0x27, 8, XRDeliveryPath::EspNow, false, 1100));
    const auto *recovery = sm.find(0x27, 8);
    assert(recovery && recovery->phase == XRDeliveryPhase::RecoveryQueued);
    assert(sm.canStartSecondary(0x27, 8, XRDeliveryPath::XBee));
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


static void duplicate_primary_failure_is_idempotent()
{
    XRDeliveryStateMachine sm;
    assert(sm.begin(0x25, 6, 1000));
    assert(sm.markPrimaryFailed(0x25, 6, 1500));
    assert(sm.markPrimaryFailed(0x25, 6, 1501));

    const auto *entry = sm.find(0x25, 6);
    assert(entry != nullptr);
    assert(entry->primaryFailures == 1);
    assert(entry->phase == XRDeliveryPhase::RecoveryQueued);
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
    unacked_assist_does_not_block_recovery_after_lora_failure();
    primary_failure_waits_for_inflight_assist();
    failed_secondary_returns_to_recovery();
    second_sidecar_cannot_race_active_one();
    expiry_is_terminal();
    return 0;
}
