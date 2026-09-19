#include "v6/V6Policy.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace meshoffgrid::v6;

int main()
{
    auto state = V6Policy::defaults();
    assert(V6Policy::valid(state));
    assert(V6Policy::connectionMode(state) == ConnectionMode::OffGrid);
    assert(V6Policy::privacyProfile(state) == PrivacyProfile::Private);
    assert(V6Policy::allowsOffGrid(state));
    assert(!V6Policy::allowsInternet(state));

    state.connectionMode = static_cast<uint8_t>(ConnectionMode::Internet);
    ++state.generation;
    V6Policy::seal(state);
    assert(V6Policy::valid(state));
    assert(V6Policy::internetOnly(state));
    assert(!V6Policy::allowsOffGrid(state));

    RuntimeCapabilities cap{};
    cap.wifiConfigured = false;
    auto decision = V6Policy::evaluateInternet(state, cap);
    assert(!decision.allowed);
    assert(decision.reason == PolicyRejectReason::WifiNotConfigured);

    cap.wifiConfigured = true;
    decision = V6Policy::evaluateInternet(state, cap);
    assert(decision.allowed);

    state.tunnelPolicy = static_cast<uint8_t>(TunnelPolicy::Required);
    ++state.generation;
    V6Policy::seal(state);
    decision = V6Policy::evaluateInternet(state, cap);
    assert(!decision.allowed);
    assert(decision.reason == PolicyRejectReason::TunnelRequiredButUnavailable);

    cap.privacyTunnelReady = true;
    decision = V6Policy::evaluateInternet(state, cap);
    assert(decision.allowed);

    state.connectionMode = static_cast<uint8_t>(ConnectionMode::Smart);
    state.privacyProfile = static_cast<uint8_t>(PrivacyProfile::Maximum);
    ++state.generation;
    V6Policy::seal(state);
    assert(V6Policy::smartMode(state));
    assert(V6Policy::allowsInternet(state));
    assert(V6Policy::allowsOffGrid(state));
    assert(V6Policy::maximumPrivacy(state));

    const auto goodChecksum = state.checksum;
    state.connectionMode = 99;
    assert(!V6Policy::valid(state));
    state.connectionMode = static_cast<uint8_t>(ConnectionMode::Smart);
    state.checksum = goodChecksum ^ 0x01020304u;
    assert(!V6Policy::valid(state));

    std::cout << "V6 policy tests passed\n";
    return 0;
}
