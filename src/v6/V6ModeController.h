#pragma once

#include "V6Policy.h"

namespace meshoffgrid::v6 {

class V6ModeController
{
  public:
    static bool begin();

    static const PolicyState &state();
    static ConnectionMode connectionMode();
    static PrivacyProfile privacyProfile();
    static TunnelPolicy tunnelPolicy();

    static bool setConnectionMode(ConnectionMode mode);
    static bool setPrivacyProfile(PrivacyProfile profile);
    static bool setTunnelPolicy(TunnelPolicy policy);

    static bool wifiCredentialsConfigured();

    // Runtime path decisions. These are the single source of truth used by
    // radio/MQTT sidecars in V6 builds.
    static bool internetTransportActive();
    static bool internetOnlyActive();
    static bool offGridTransportActive();
    static bool smartModeActive();

    // The production V6.0 build deliberately ships without an unproven full
    // VPN implementation. A future reviewed tunnel provider sets this runtime
    // capability only after a real handshake succeeds.
    static void setPrivacyTunnelReady(bool ready);
    static bool privacyTunnelReady();

    static PolicyRejectReason lastRejectReason();

  private:
    static bool apply(const PolicyState &policy, bool persistMeshtastic);
    static bool commit(PolicyState next);
};

} // namespace meshoffgrid::v6
