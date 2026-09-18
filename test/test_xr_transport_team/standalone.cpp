#include "xr/XRTransportTeam.h"

#include <cassert>

using namespace meshoffgrid::xr;

static void test_best_route_and_single_assist()
{
    XRTransportTeam team;
    team.reportRoute(XRTeamTransport::EspNow, 0x22, 82, true, 1000);
    team.reportRoute(XRTeamTransport::XBee, 0x22, 70, true, 1000);

    assert(team.preferredTransport(0x22, 1001) == XRTeamTransport::EspNow);
    assert(team.allowAssist(XRTeamTransport::EspNow, 0x22, 7, 1002));
    assert(!team.allowAssist(XRTeamTransport::XBee, 0x22, 7, 1003));
}

static void test_recovery_rotates_after_failure()
{
    XRTransportTeam team;
    team.reportRoute(XRTeamTransport::EspNow, 0x33, 90, true, 1000);
    team.reportRoute(XRTeamTransport::XBee, 0x33, 75, true, 1000);

    // First call opens a tiny scoring window; neither task wins by scheduler order.
    assert(!team.claimRecovery(XRTeamTransport::EspNow, 0x33, 9, 2000));
    assert(team.claimRecovery(XRTeamTransport::EspNow, 0x33, 9,
                              2000 + XRTransportTeam::RECOVERY_ARBITRATION_MS + 1));
    team.reportRecoveryResult(XRTeamTransport::EspNow, 0x33, 9, false,
                              2000 + XRTransportTeam::RECOVERY_ARBITRATION_MS + 2);

    assert(!team.claimRecovery(XRTeamTransport::EspNow, 0x33, 9,
                               2000 + XRTransportTeam::RECOVERY_ARBITRATION_MS + 3));
    assert(team.claimRecovery(XRTeamTransport::XBee, 0x33, 9,
                              2000 + XRTransportTeam::RECOVERY_ARBITRATION_MS + 3));
}

static void test_carrier_acceptance_waits_for_end_to_end_ack()
{
    XRTransportTeam team;
    team.reportRoute(XRTeamTransport::EspNow, 0x44, 85, true, 1000);
    team.reportRoute(XRTeamTransport::XBee, 0x44, 80, true, 1000);

    assert(!team.claimRecovery(XRTeamTransport::EspNow, 0x44, 11, 2000));
    const uint32_t firstClaim = 2000 + XRTransportTeam::RECOVERY_ARBITRATION_MS + 1;
    assert(team.claimRecovery(XRTeamTransport::EspNow, 0x44, 11, firstClaim));
    team.reportRecoveryResult(XRTeamTransport::EspNow, 0x44, 11, true, firstClaim + 100);

    assert(!team.claimRecovery(XRTeamTransport::XBee, 0x44, 11, firstClaim + 1000));
    assert(!team.claimRecovery(XRTeamTransport::EspNow, 0x44, 11, firstClaim + 1000));

    // After the ACK wait expires, the previously accepted path gets a short
    // cooldown, so the other healthy transport gets the first recovery chance.
    assert(team.claimRecovery(XRTeamTransport::XBee, 0x44, 11,
                              firstClaim + 100 + XRTransportTeam::ACK_WAIT_MS + 1));

    team.markDelivered(0x44, 11, firstClaim + 100 + XRTransportTeam::ACK_WAIT_MS + 2);
}


static void test_end_to_end_ack_creates_path_hysteresis()
{
    XRTransportTeam team;
    team.reportRoute(XRTeamTransport::EspNow, 0x66, 78, true, 1000, XRTeamRouteKind::Direct);
    team.reportRoute(XRTeamTransport::XBee, 0x66, 88, true, 1000, XRTeamRouteKind::Direct);

    assert(!team.claimRecovery(XRTeamTransport::XBee, 0x66, 21, 2000));
    const uint32_t claimAt = 2000 + XRTransportTeam::RECOVERY_ARBITRATION_MS + 1;
    assert(team.claimRecovery(XRTeamTransport::XBee, 0x66, 21, claimAt));
    team.reportRecoveryResult(XRTeamTransport::XBee, 0x66, 21, true, claimAt + 10);
    team.markDelivered(0x66, 21, claimAt + 100);

    // A tiny raw-score change should not immediately thrash away from the path
    // that just proved end-to-end delivery.
    team.reportRoute(XRTeamTransport::EspNow, 0x66, 82, true, claimAt + 101, XRTeamRouteKind::Direct);
    team.reportRoute(XRTeamTransport::XBee, 0x66, 79, true, claimAt + 101, XRTeamRouteKind::Direct);
    assert(team.preferredTransport(0x66, claimAt + 102) == XRTeamTransport::XBee);
}

static void test_ambient_rf_penalizes_bridge_not_direct_carriers()
{
    XRTransportTeam team;
    team.reportEnvironment(80, 55, -84, 1000);
    team.reportRoute(XRTeamTransport::EspNow, 0x77, 70, true, 1001, XRTeamRouteKind::Bridge);
    team.reportRoute(XRTeamTransport::XBee, 0x77, 68, true, 1001, XRTeamRouteKind::Direct);

    // Unknown ambient RF is not a carrier. It only makes a LoRa-dependent
    // bridge less attractive than a direct compatible sidecar path.
    assert(team.preferredTransport(0x77, 1002) == XRTeamTransport::XBee);
}


static void test_low_battery_never_overrides_clearly_better_quality()
{
    XRTransportTeam team;
    team.reportEnvironment(8, 10, -118, 1000);
    team.reportRoute(XRTeamTransport::EspNow, 0x88, 68, true, 1001, XRTeamRouteKind::Direct);
    team.reportRoute(XRTeamTransport::XBee, 0x88, 86, true, 1001, XRTeamRouteKind::Direct);

    // Delivery quality dominates battery savings.
    assert(team.preferredTransport(0x88, 1002) == XRTeamTransport::XBee);
}

static void test_low_battery_only_breaks_near_equal_quality_ties()
{
    XRTransportTeam team;
    team.reportEnvironment(8, 10, -118, 1000);
    team.reportRoute(XRTeamTransport::EspNow, 0x89, 80, true, 1001, XRTeamRouteKind::Direct);
    team.reportRoute(XRTeamTransport::XBee, 0x89, 82, true, 1001, XRTeamRouteKind::Direct);

    // Inside the tiny equivalence band the lower-energy integrated radio may
    // win because predicted delivery quality is effectively the same.
    assert(team.preferredTransport(0x89, 1002) == XRTeamTransport::EspNow);
}


static void test_learning_snapshot_roundtrip_and_corruption_rejection()
{
    XRTransportTeam source;
    source.reportRoute(XRTeamTransport::XBee, 0x90, 90, true, 1000);
    assert(!source.claimRecovery(XRTeamTransport::XBee, 0x90, 30, 1100));
    const uint32_t claimAt = 1100 + XRTransportTeam::RECOVERY_ARBITRATION_MS + 1;
    assert(source.claimRecovery(XRTeamTransport::XBee, 0x90, 30, claimAt));
    source.reportRecoveryResult(XRTeamTransport::XBee, 0x90, 30, true, claimAt + 10);
    source.markDelivered(0x90, 30, claimAt + 100);

    uint32_t generation = 0;
    const auto snapshot = source.learningSnapshot(generation);
    assert(generation != 0);

    XRTransportTeam restored;
    assert(restored.restoreLearning(snapshot, 5000));
    restored.reportRoute(XRTeamTransport::EspNow, 0x90, 90, true, 5001);
    restored.reportRoute(XRTeamTransport::XBee, 0x90, 88, true, 5001);
    assert(restored.preferredTransport(0x90, 5002) == XRTeamTransport::XBee);

    auto corrupt = snapshot;
    corrupt.records[0].destination ^= 1u;
    XRTransportTeam rejected;
    assert(!rejected.restoreLearning(corrupt, 5000));
}

static void test_learning_arriving_during_save_stays_dirty()
{
    XRTransportTeam team;
    team.reportRoute(XRTeamTransport::EspNow, 0x91, 85, true, 1000);
    assert(team.allowAssist(XRTeamTransport::EspNow, 0x91, 31, 1001));
    team.reportAssistResult(XRTeamTransport::EspNow, 0x91, 31, true, 1002);
    assert(team.learningDirty());

    uint32_t capturedGeneration = 0;
    (void)team.learningSnapshot(capturedGeneration);

    team.markCancelled(0x91, 31, 1003);
    team.reportRoute(XRTeamTransport::XBee, 0x92, 90, true, 1004);
    assert(team.allowAssist(XRTeamTransport::XBee, 0x92, 32, 1005));
    team.reportAssistResult(XRTeamTransport::XBee, 0x92, 32, false, 1006);

    team.markLearningPersisted(capturedGeneration);
    assert(team.learningDirty());

    uint32_t newestGeneration = 0;
    (void)team.learningSnapshot(newestGeneration);
    team.markLearningPersisted(newestGeneration);
    assert(!team.learningDirty());
}

static void test_stale_routes_are_ignored()
{
    XRTransportTeam team;
    team.reportRoute(XRTeamTransport::EspNow, 0x55, 95, true, 1000);
    assert(team.preferredTransport(0x55, 1001) == XRTeamTransport::EspNow);
    assert(team.preferredTransport(0x55, 1000 + XRTransportTeam::ROUTE_REPORT_TTL_MS + 1) ==
           XRTeamTransport::None);
}

int main()
{
    test_best_route_and_single_assist();
    test_recovery_rotates_after_failure();
    test_carrier_acceptance_waits_for_end_to_end_ack();
    test_end_to_end_ack_creates_path_hysteresis();
    test_ambient_rf_penalizes_bridge_not_direct_carriers();
    test_low_battery_never_overrides_clearly_better_quality();
    test_low_battery_only_breaks_near_equal_quality_ties();
    test_learning_snapshot_roundtrip_and_corruption_rejection();
    test_learning_arriving_during_save_stays_dirty();
    test_stale_routes_are_ignored();
    return 0;
}
