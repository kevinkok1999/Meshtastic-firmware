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

    assert(team.claimRecovery(XRTeamTransport::EspNow, 0x33, 9, 2000));
    team.reportRecoveryResult(XRTeamTransport::EspNow, 0x33, 9, false, 2001);

    assert(!team.claimRecovery(XRTeamTransport::EspNow, 0x33, 9, 2002));
    assert(team.claimRecovery(XRTeamTransport::XBee, 0x33, 9, 2002));
}

static void test_carrier_acceptance_waits_for_end_to_end_ack()
{
    XRTransportTeam team;
    team.reportRoute(XRTeamTransport::EspNow, 0x44, 85, true, 1000);
    team.reportRoute(XRTeamTransport::XBee, 0x44, 80, true, 1000);

    assert(team.claimRecovery(XRTeamTransport::EspNow, 0x44, 11, 2000));
    team.reportRecoveryResult(XRTeamTransport::EspNow, 0x44, 11, true, 2100);

    assert(!team.claimRecovery(XRTeamTransport::XBee, 0x44, 11, 3000));
    assert(!team.claimRecovery(XRTeamTransport::EspNow, 0x44, 11, 3000));

    // After the ACK wait expires, the previously accepted path gets a short
    // cooldown, so the other healthy transport gets the first recovery chance.
    assert(team.claimRecovery(XRTeamTransport::XBee, 0x44, 11, 62101));

    team.markDelivered(0x44, 11);
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
    test_stale_routes_are_ignored();
    return 0;
}
