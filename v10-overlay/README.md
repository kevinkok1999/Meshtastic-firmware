# V10 implementation overlay

V10.0 is a two-device DirectLink release for stock LILYGO T-Deck Plus hardware.

Release-enabled paths:
- MeshCore / SX1262 LoRa;
- ESP-NOW Long Range;
- Wi-Fi Long Range Direct over an authenticated pair-only UDP link.

Wi-Fi LR Direct:
- requires no router and no internet;
- chooses AP/STA role deterministically from the two cryptographic identities;
- derives the Wi-Fi credential from the ECDH pair secret;
- encrypts/authenticates the application payload again;
- uses the same fixed LR Wi-Fi channel as the existing ESP-NOW LR path;
- probes reachability before it enters route selection;
- falls back to normal LoRa on ACK timeout.

V10 route policy understands five logical bearers and two physical RF resources.

GFSK Direct and BLE Coded are present as architecture/capability gates but are intentionally disabled in V10.0 production routing until two real T-Decks prove safe modem/coexistence restoration. This prevents an experimental path from stranding the known-working LoRa/ESP-NOW stack.

No V1-V9 source or asset is modified.
