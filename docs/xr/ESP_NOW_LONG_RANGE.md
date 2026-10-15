# ESP-NOW Long Range / Range-First T-Deck profile

The MeshOffGridNL production T-Deck profile enables Espressif Wi-Fi Long Range (LR) as a direct ESP-NOW sidecar while keeping normal Meshtastic/LoRa unchanged and primary.

## Direct-range behavior

- The station interface keeps normal 802.11 B/G/N support and adds `WIFI_PROTOCOL_LR`; it is not switched to LR-only mode.
- New XR T-Deck peers advertise LR capability in the optional HELLO payload.
- Discovery sends both a normal 1 Mbps HELLO and an LR 250 kbps HELLO.
- Known LR-capable peers use `WIFI_PHY_RATE_LORA_250K` for ESP-NOW.
- Older XR peers that do not advertise the capability remain on the normal 1 Mbps ESP-NOW rate.
- LR frame delivery gets one bounded retry at the fragment level.
- Weak but fresh LR peers remain eligible in the route score instead of being discarded solely because their RSSI is near the old normal-rate scoring floor.
- Range-first disables Wi-Fi power saving when the XR sidecar starts, improving receiver availability at the cost of battery life.

## Failure behavior

Every LR setup step fails open to standard ESP-NOW. If ESP-NOW itself is unavailable, normal Meshtastic/LoRa continues. No RF power limit, regional LoRa setting, or Meshtastic channel/modem preset is overridden.

## Compatibility

The normal LoRa packet format, encryption, channels, ACK behavior and P1/P1 Pro compatibility remain unchanged. ESP-NOW LR is only an additional direct path between compatible ESP32-series XR peers.
