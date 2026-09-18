#include "XRInternetMode.h"

#if defined(MESHOFFGRID_ENABLE_MANUAL_INTERNET_MODE)

#include "mesh/Channels.h"
#include "mesh/NodeDB.h"

namespace meshoffgrid::xr {

XRInternetRadioGate xrInternetRadioGate;

bool internetModeActive()
{
    return config.network.wifi_enabled && moduleConfig.mqtt.enabled;
}

bool wifiCredentialsConfigured()
{
    return config.network.wifi_ssid[0] != '\0';
}

XRCommunicationMode communicationMode()
{
    return internetModeActive() ? XRCommunicationMode::Internet : XRCommunicationMode::OffGrid;
}

static void setMqttChannelFlags(bool enabled)
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

bool setCommunicationMode(XRCommunicationMode mode)
{
    if (mode == XRCommunicationMode::Internet) {
        if (!wifiCredentialsConfigured()) {
            LOG_WARN("V5 Internet mode refused: Wi-Fi SSID is not configured");
            return false;
        }

        // Internet layer: direct Wi-Fi + encrypted Meshtastic MQTT.
        // No automatic LoRa/ESP-NOW/XBee fallback is permitted in this mode.
        config.network.wifi_enabled = true;
        config.bluetooth.enabled = false;
        config.lora.ignore_mqtt = false;

        moduleConfig.mqtt.enabled = true;
        moduleConfig.mqtt.encryption_enabled = true;
        moduleConfig.mqtt.proxy_to_client_enabled = false;
        moduleConfig.mqtt.map_reporting_enabled = false;
        setMqttChannelFlags(true);
    } else {
        // Off-grid layer: preserve the V2 radio system and make cloud transport
        // explicitly inactive. Stored Wi-Fi credentials are retained for the
        // next time the user manually selects Internet mode.
        config.network.wifi_enabled = false;
        config.bluetooth.enabled = true;
        config.lora.ignore_mqtt = true;

        moduleConfig.mqtt.enabled = false;
        moduleConfig.mqtt.proxy_to_client_enabled = false;
        moduleConfig.mqtt.map_reporting_enabled = false;
        setMqttChannelFlags(false);
    }

    const bool saved =
        nodeDB->saveToDisk(SEGMENT_CONFIG | SEGMENT_MODULECONFIG | SEGMENT_CHANNELS);
    if (!saved)
        LOG_ERROR("V5 communication mode could not be persisted");
    return saved;
}

RadioTxHook::PreTxAction XRInternetRadioGate::beforeTransmit(RadioInterface *, meshtastic_MeshPacket *packet)
{
    if (packet && internetModeActive())
        return PRETX_DROP;
    return PRETX_SEND;
}

} // namespace meshoffgrid::xr

#endif
