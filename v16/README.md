# MeshOffGridNL V16 — Wi-Fi Connection Engine

V16 is a focused T-Deck/T-Deck Plus Wi-Fi reliability release built on V15.

The field symptom was: Android hotspot is visible, Connect is pressed, V15 no longer crashes, but the UI can remain on Connecting without reaching an IP address.

V16 addresses the state-machine causes found in the actual V15 source:

- A user Connect action has priority over Wi-Fi scanning.
- Queued scans are cancelled before association; an already-running async scan is aborted by the worker that owns it.
- Closing the join sheet no longer immediately rebuilds the network list and starts another scan under WPA/DHCP.
- Arduino auto-reconnect remains disabled after scans, so there is one reconnect owner.
- A scanned BSSID/channel hint is used once only. Retry 2+ falls back to SSID-only all-channel association.
- The last ESP-IDF disconnect reason is preserved across retries instead of being erased before every begin().
- The UI exposes association and IP-acquisition phases rather than an endless generic Connecting label.
- Foreground join uses three 12-second attempts; after that it releases the UI and continues slower 60-second background recovery.
- V15's Wi-Fi/Bluetooth power-management crash fix remains unchanged.
- V11 encrypted global chat and LoRa fallback remain intact.

The build stays on the pinned WadaMesh beta_83 / Arduino-ESP32 2.0.17 baseline so the Wi-Fi state-machine repair is isolated from a risky framework migration.

CI must pass V11, V12, V13, V15 and V16 contracts and compile the real LilyGo_TDeck_companion_radio_touch target. Physical Android-hotspot testing on the affected T-Deck Plus is still required before this RC can be called stable.
