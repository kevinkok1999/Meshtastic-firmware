#pragma once

#include <cstddef>
#include <cstdint>

namespace meshoffgrid
{

enum class ConnectivityState : uint8_t {
    NetworkDisabled = 0,
    RadioReady,
    Connecting,
    Associated,
    LocalReady,
    Recovering,
    Degraded,
};

enum class ConnectivityError : uint8_t {
    None = 0,
    SsidNotFound,
    AuthFailed,
    DhcpTimeout,
    BeaconTimeout,
    ConnectionLost,
    Unknown,
};

struct ConnectivitySnapshot {
    ConnectivityState state = ConnectivityState::NetworkDisabled;
    ConnectivityError error = ConnectivityError::None;
    bool networkConfigured = false;
    bool wifiConnected = false;
    bool hasIpv4 = false;
    bool directLinkReady = false;
    uint8_t wifiChannel = 0;
    int8_t wifiRssi = 0;
    uint8_t disconnectReason = 0;
    uint8_t directPeers = 0;
    uint32_t stateSinceMs = 0;
};

void initV14ConnectivitySupervisor();
ConnectivitySnapshot v14ConnectivitySnapshot();
const char *v14ConnectivityStateName(ConnectivityState state);
const char *v14ConnectivityErrorName(ConnectivityError error);

} // namespace meshoffgrid
