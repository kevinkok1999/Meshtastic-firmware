#pragma once

#include "configuration.h"

#if defined(MESHOFFGRID_ENABLE_MANUAL_INTERNET_MODE)

#include "mesh/RadioTxHook.h"

namespace meshoffgrid::xr {

enum class XRCommunicationMode : uint8_t {
    OffGrid = 0,
    Internet = 1,
};

// V5 deliberately has two user-selected layers. It never auto-falls back
// between Internet and the V2 off-grid transports.
bool internetModeActive();
bool wifiCredentialsConfigured();
XRCommunicationMode communicationMode();

// Persists the selected mode. The caller should reboot afterwards so Wi-Fi,
// MQTT and the V2 sidecars start in one clean, deterministic configuration.
bool setCommunicationMode(XRCommunicationMode mode);

// Runs after Router::send() has already offered locally-originated packets to
// MQTT. In Internet mode this prevents the same packet from also going over
// LoRa. It also prevents MQTT downlinks from being bridged back onto LoRa.
class XRInternetRadioGate final : public RadioTxHook
{
  public:
    PreTxAction beforeTransmit(RadioInterface *, meshtastic_MeshPacket *packet) override;
};

extern XRInternetRadioGate xrInternetRadioGate;

} // namespace meshoffgrid::xr

#endif
