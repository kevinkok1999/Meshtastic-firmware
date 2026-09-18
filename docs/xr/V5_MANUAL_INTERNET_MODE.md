# MeshOffGridNL Version 5 — Manual Internet + Off-grid

Version 5 is an isolated derivative of Version 2. Versions 1–4 are not modified.

## Communication layers

### Off-grid
Default mode. Wi-Fi/MQTT is disabled. The Version 2 transport stack remains active:
- Meshtastic/LoRa
- XR ESP-NOW sidecar
- optional XBee XR 868 sidecar
- encrypted store/carry/forward and adaptive delivery

### Internet (Wi-Fi)
User-selected mode. There is no automatic fallback to off-grid transports.
- joins the Wi-Fi network already configured in Meshtastic settings
- enables encrypted Meshtastic MQTT
- enables MQTT uplink/downlink on enabled chat channels
- uses the same Meshtastic packets, channels, identities and T-Deck chat UI
- suppresses physical LoRa transmission after MQTT has accepted/queued the local packet
- keeps ESP-NOW and XBee sidecars inactive
- disables MQTT map reporting
- stores a packet in Meshtastic's MQTT queue if the broker is temporarily disconnected

Both endpoints need Internet access and matching Meshtastic channel/key configuration for channel messages to be readable on both sides.

## Switching mode on the T-Deck

System → Communication Mode → Off-grid / Internet (Wi-Fi)

A reboot applies the selected layer cleanly. Internet mode is refused when no Wi-Fi SSID has been configured. Wi-Fi credentials are retained when switching back to Off-grid.

## Deliberate non-goals

- no automatic Internet ↔ off-grid failover
- no reuse of arbitrary third-party Wi-Fi networks as relays
- no plaintext MQTT payloads
- no automatic map/location publishing
