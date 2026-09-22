#include "MeshOffGridConnectivity.h"

#if defined(MESHOFFGRID_V14) && defined(ARCH_ESP32)

#include "MeshOffGridDirectLink.h"
#include "concurrency/Periodic.h"
#include "configuration.h"
#include "mesh/wifi/WiFiAPClient.h"

#include <WiFi.h>
#include <algorithm>
#include <esp_wifi.h>

namespace meshoffgrid
{
namespace
{

constexpr uint32_t kDhcpTimeoutMs = 15000;

portMUX_TYPE snapshotMux = portMUX_INITIALIZER_UNLOCKED;
ConnectivitySnapshot snapshot;
concurrency::Periodic *connectivityThread = nullptr;
uint32_t associatedSinceMs = 0;

ConnectivityError classifyDisconnect(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
        return ConnectivityError::SsidNotFound;
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return ConnectivityError::AuthFailed;
    case WIFI_REASON_BEACON_TIMEOUT:
        return ConnectivityError::BeaconTimeout;
    case WIFI_REASON_ASSOC_FAIL:
        return ConnectivityError::ConnectionLost;
    default:
        return reason == 0 ? ConnectivityError::None : ConnectivityError::Unknown;
    }
}

bool hasIpv4Address()
{
    return static_cast<uint32_t>(WiFi.localIP()) != 0;
}

int32_t connectivityTick()
{
    ConnectivitySnapshot next;
    next.networkConfigured = config.network.wifi_enabled && config.network.wifi_ssid[0] != '\0';
    next.wifiConnected = WiFi.isConnected();
    next.hasIpv4 = next.wifiConnected && hasIpv4Address();
    next.directLinkReady = isV14DirectLinkReady();
    next.directPeers = static_cast<uint8_t>(std::min<size_t>(255, v14DirectLinkPeerCount()));
    next.wifiChannel = WiFi.getMode() == WIFI_OFF ? 0 : WiFi.channel();
    next.wifiRssi = next.wifiConnected ? static_cast<int8_t>(WiFi.RSSI()) : 0;
    next.disconnectReason = getWifiDisconnectReason();

    const uint32_t now = millis();
    if (!next.networkConfigured) {
        associatedSinceMs = 0;
        next.state = next.directLinkReady ? ConnectivityState::RadioReady : ConnectivityState::NetworkDisabled;
    } else if (next.wifiConnected) {
        if (next.hasIpv4) {
            associatedSinceMs = 0;
            next.state = ConnectivityState::LocalReady;
        } else {
            if (associatedSinceMs == 0)
                associatedSinceMs = now;
            next.state = ConnectivityState::Associated;
            if (static_cast<uint32_t>(now - associatedSinceMs) >= kDhcpTimeoutMs) {
                next.state = ConnectivityState::Degraded;
                next.error = ConnectivityError::DhcpTimeout;
            }
        }
    } else {
        associatedSinceMs = 0;
        const wl_status_t status = WiFi.status();
        if (status == WL_NO_SSID_AVAIL) {
            next.state = ConnectivityState::Degraded;
            next.error = ConnectivityError::SsidNotFound;
        } else if (status == WL_CONNECT_FAILED) {
            next.state = ConnectivityState::Degraded;
            next.error = ConnectivityError::AuthFailed;
        } else if (status == WL_CONNECTION_LOST) {
            next.state = ConnectivityState::Recovering;
            next.error = ConnectivityError::ConnectionLost;
        } else if (needReconnect) {
            next.state = ConnectivityState::Recovering;
            next.error = classifyDisconnect(next.disconnectReason);
        } else {
            next.state = ConnectivityState::Connecting;
            next.error = classifyDisconnect(next.disconnectReason);
        }
    }

    bool changed = false;
    portENTER_CRITICAL(&snapshotMux);
    changed = next.state != snapshot.state || next.error != snapshot.error || next.wifiChannel != snapshot.wifiChannel ||
              next.directPeers != snapshot.directPeers;
    next.stateSinceMs =
        (next.state == snapshot.state && snapshot.stateSinceMs != 0) ? snapshot.stateSinceMs : now;
    snapshot = next;
    portEXIT_CRITICAL(&snapshotMux);

    if (changed) {
        LOG_INFO("V14 connectivity state=%s error=%s channel=%u directPeers=%u", v14ConnectivityStateName(next.state),
                 v14ConnectivityErrorName(next.error), next.wifiChannel, next.directPeers);
    }

    return 500;
}

} // namespace

void initV14ConnectivitySupervisor()
{
    if (!connectivityThread)
        connectivityThread = new concurrency::Periodic("V14Connect", connectivityTick);
}

ConnectivitySnapshot v14ConnectivitySnapshot()
{
    portENTER_CRITICAL(&snapshotMux);
    ConnectivitySnapshot copy = snapshot;
    portEXIT_CRITICAL(&snapshotMux);
    return copy;
}

const char *v14ConnectivityStateName(ConnectivityState state)
{
    switch (state) {
    case ConnectivityState::NetworkDisabled:
        return "NETWORK_DISABLED";
    case ConnectivityState::RadioReady:
        return "RADIO_READY";
    case ConnectivityState::Connecting:
        return "CONNECTING";
    case ConnectivityState::Associated:
        return "ASSOCIATED";
    case ConnectivityState::LocalReady:
        return "LOCAL_READY";
    case ConnectivityState::Recovering:
        return "RECOVERING";
    case ConnectivityState::Degraded:
        return "DEGRADED";
    }
    return "UNKNOWN";
}

const char *v14ConnectivityErrorName(ConnectivityError error)
{
    switch (error) {
    case ConnectivityError::None:
        return "NONE";
    case ConnectivityError::SsidNotFound:
        return "SSID_NOT_FOUND";
    case ConnectivityError::AuthFailed:
        return "AUTH_FAILED";
    case ConnectivityError::DhcpTimeout:
        return "DHCP_TIMEOUT";
    case ConnectivityError::BeaconTimeout:
        return "BEACON_TIMEOUT";
    case ConnectivityError::ConnectionLost:
        return "CONNECTION_LOST";
    case ConnectivityError::Unknown:
        return "UNKNOWN";
    }
    return "UNKNOWN";
}

} // namespace meshoffgrid

#else

namespace meshoffgrid
{

void initV14ConnectivitySupervisor() {}
ConnectivitySnapshot v14ConnectivitySnapshot() { return {}; }
const char *v14ConnectivityStateName(ConnectivityState) { return "UNAVAILABLE"; }
const char *v14ConnectivityErrorName(ConnectivityError) { return "UNAVAILABLE"; }

} // namespace meshoffgrid

#endif
