#include "V6ModeController.h"

#if defined(MESHOFFGRID_ENABLE_V6)

#include "V6PolicyStore.h"
#include "mesh/Channels.h"
#include "mesh/NodeDB.h"

namespace meshoffgrid::v6 {

namespace {
PolicyState currentPolicy = V6Policy::defaults();
bool initialized = false;
bool tunnelReady = false;
PolicyRejectReason rejectReason = PolicyRejectReason::None;

void setMqttChannelFlags(bool enabled)
{
    for (ChannelIndex i = 0; i < channels.getNumChannels(); ++i) {
        auto channel = channels.getByIndex(i);
        if (!channel.has_settings || channel.role == meshtastic_Channel_Role_DISABLED)
            continue;
        channel.settings.uplink_enabled = enabled;
        channel.settings.downlink_enabled = enabled;
        channels.setChannel(channel);
    }
}

RuntimeCapabilities capabilities()
{
    RuntimeCapabilities out{};
    out.wifiConfigured = config.network.wifi_ssid[0] != '\0';
    out.privacyTunnelReady = tunnelReady;
    return out;
}

bool effectiveInternetAllowed(const PolicyState &policy, PolicyRejectReason *reason = nullptr)
{
    if (!V6Policy::allowsInternet(policy)) {
        if (reason)
            *reason = PolicyRejectReason::None;
        return false;
    }

    const auto decision = V6Policy::evaluateInternet(policy, capabilities());
    if (reason)
        *reason = decision.reason;
    return decision.allowed;
}
} // namespace

bool V6ModeController::begin()
{
    if (initialized)
        return true;

    PolicyState loaded{};
    if (V6PolicyStore::load(loaded) && V6Policy::valid(loaded)) {
        currentPolicy = loaded;
    } else {
        currentPolicy = V6Policy::defaults();
        (void)V6PolicyStore::save(currentPolicy);
    }

    initialized = true;

    // Boot must fail closed. If a previously selected Internet-only mode is
    // currently impossible (missing Wi-Fi or a required tunnel), keep the
    // requested policy on disk but run with all Internet egress disabled.
    if (!apply(currentPolicy, false)) {
        PolicyState safe = currentPolicy;
        safe.connectionMode = static_cast<uint8_t>(ConnectionMode::OffGrid);
        V6Policy::seal(safe);
        (void)apply(safe, false);
        LOG_WARN("V6 requested Internet policy unavailable at boot; runtime is fail-closed Off-grid");
    }

    return true;
}

const PolicyState &V6ModeController::state()
{
    (void)begin();
    return currentPolicy;
}

ConnectionMode V6ModeController::connectionMode()
{
    return V6Policy::connectionMode(state());
}

PrivacyProfile V6ModeController::privacyProfile()
{
    return V6Policy::privacyProfile(state());
}

TunnelPolicy V6ModeController::tunnelPolicy()
{
    return V6Policy::tunnelPolicy(state());
}

bool V6ModeController::wifiCredentialsConfigured()
{
    return config.network.wifi_ssid[0] != '\0';
}

bool V6ModeController::internetTransportActive()
{
    (void)begin();
    return effectiveInternetAllowed(currentPolicy) && config.network.wifi_enabled && moduleConfig.mqtt.enabled;
}

bool V6ModeController::internetOnlyActive()
{
    return connectionMode() == ConnectionMode::Internet && internetTransportActive();
}

bool V6ModeController::offGridTransportActive()
{
    (void)begin();
    if (connectionMode() == ConnectionMode::OffGrid)
        return true;
    if (connectionMode() == ConnectionMode::Smart)
        return true;
    // If an Internet-only request failed closed during boot, LoRa remains
    // available as local runtime safety but no automatic Internet fallback is
    // performed. User must explicitly choose a new mode to persist a change.
    return !internetTransportActive();
}

bool V6ModeController::smartModeActive()
{
    return connectionMode() == ConnectionMode::Smart;
}

void V6ModeController::setPrivacyTunnelReady(bool ready)
{
    tunnelReady = ready;

    // If a required tunnel disappears while Internet-only mode is active,
    // immediately close the Internet path. Do not silently downgrade.
    if (initialized && V6Policy::tunnelPolicy(currentPolicy) == TunnelPolicy::Required && !ready) {
        moduleConfig.mqtt.enabled = false;
        config.network.wifi_enabled = false;
        setMqttChannelFlags(false);
        rejectReason = PolicyRejectReason::TunnelRequiredButUnavailable;
    }
}

bool V6ModeController::privacyTunnelReady()
{
    return tunnelReady;
}

PolicyRejectReason V6ModeController::lastRejectReason()
{
    return rejectReason;
}

bool V6ModeController::apply(const PolicyState &policy, bool persistMeshtastic)
{
    if (!V6Policy::valid(policy)) {
        rejectReason = PolicyRejectReason::InvalidState;
        return false;
    }

    PolicyRejectReason pathReason = PolicyRejectReason::None;
    const bool wantsInternet = V6Policy::allowsInternet(policy);
    const bool internetAllowed = effectiveInternetAllowed(policy, &pathReason);

    if (V6Policy::connectionMode(policy) == ConnectionMode::Internet && !internetAllowed) {
        rejectReason = pathReason;
        return false;
    }

    // Privacy floor for every V6 Internet-capable profile.
    moduleConfig.mqtt.encryption_enabled = true;
    moduleConfig.mqtt.tls_enabled = true;
    moduleConfig.mqtt.proxy_to_client_enabled = false;
    moduleConfig.mqtt.map_reporting_enabled = false;
    moduleConfig.mqtt.has_map_report_settings = true;
    moduleConfig.mqtt.map_report_settings.should_report_location = false;

    const bool maxPrivacy = V6Policy::maximumPrivacy(policy);

    switch (V6Policy::connectionMode(policy)) {
    case ConnectionMode::OffGrid:
        config.network.wifi_enabled = false;
        moduleConfig.mqtt.enabled = false;
        config.lora.ignore_mqtt = true;
        config.lora.config_ok_to_mqtt = false;
        config.bluetooth.enabled = !maxPrivacy;
        setMqttChannelFlags(false);
        break;

    case ConnectionMode::Internet:
        config.network.wifi_enabled = true;
        config.bluetooth.enabled = false;
        moduleConfig.mqtt.enabled = true;
        config.lora.ignore_mqtt = false;
        config.lora.config_ok_to_mqtt = true;
        setMqttChannelFlags(true);
        break;

    case ConnectionMode::Smart:
        // Smart is explicit opt-in. If Internet is unavailable it remains an
        // off-grid mesh; when permitted Internet is available, both paths may
        // coexist and normal packet IDs/dedup prevent duplicate chat entries.
        config.network.wifi_enabled = internetAllowed;
        config.bluetooth.enabled = internetAllowed ? false : !maxPrivacy;
        moduleConfig.mqtt.enabled = internetAllowed;
        config.lora.ignore_mqtt = !internetAllowed;
        config.lora.config_ok_to_mqtt = internetAllowed;
        setMqttChannelFlags(internetAllowed);
        break;
    }

    // Maximum privacy reduces local broadcast surface in addition to the
    // mandatory no-map/no-plaintext-MQTT floor.
    if (maxPrivacy)
        config.bluetooth.enabled = false;

    if (persistMeshtastic) {
        const bool saved = nodeDB->saveToDisk(SEGMENT_CONFIG | SEGMENT_MODULECONFIG | SEGMENT_CHANNELS);
        if (!saved) {
            LOG_ERROR("V6 Meshtastic policy state could not be persisted");
            return false;
        }
    }

    rejectReason = wantsInternet && !internetAllowed ? pathReason : PolicyRejectReason::None;
    return true;
}

bool V6ModeController::commit(PolicyState next)
{
    (void)begin();

    next.magic = V6Policy::MAGIC;
    next.version = V6Policy::VERSION;
    next.generation = currentPolicy.generation + 1u;
    V6Policy::seal(next);
    if (!V6Policy::valid(next))
        return false;

    const PolicyState previous = currentPolicy;

    // Evaluate/apply before writing the new policy. An impossible Internet-only
    // request never becomes persistent state.
    if (!apply(next, true))
        return false;

    if (!V6PolicyStore::save(next)) {
        LOG_ERROR("V6 policy file save failed; rolling runtime back");
        (void)apply(previous, true);
        return false;
    }

    currentPolicy = next;
    return true;
}

bool V6ModeController::setConnectionMode(ConnectionMode mode)
{
    PolicyState next = state();
    next.connectionMode = static_cast<uint8_t>(mode);
    return commit(next);
}

bool V6ModeController::setPrivacyProfile(PrivacyProfile profile)
{
    PolicyState next = state();
    next.privacyProfile = static_cast<uint8_t>(profile);
    return commit(next);
}

bool V6ModeController::setTunnelPolicy(TunnelPolicy policy)
{
    PolicyState next = state();
    next.tunnelPolicy = static_cast<uint8_t>(policy);
    return commit(next);
}

} // namespace meshoffgrid::v6

#endif
