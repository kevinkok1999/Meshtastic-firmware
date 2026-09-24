# MeshOffGridNL V15 — V13 Hotspot Crash Fix

V15 is intentionally a direct copy of V13 Mobile Wi-Fi with one targeted stability fix.

## Base

V15 keeps the complete V13 behavior unchanged:

- V11 encrypted worldwide direct-message bridge
- normal LoRa fallback
- V12 scrollable Wi-Fi list and Wi-Fi hardening
- V13 selected BSSID/channel handoff
- V13 all-channel SSID fallback
- V13 single reconnect owner
- V13 15-second retry interval
- V13 no-erase retry behavior
- V13 on-device disconnect diagnostics

V14 DirectLink changes are **not** included.

## The only functional V15 change

V13 called `WiFi.setSleep(false)` inside the T-Deck Wi-Fi association helper before every `WiFi.begin()`.

The pinned WadaMesh base already owns ESP32-S3 Wi-Fi/Bluetooth coexistence and applies the correct modem-sleep policy after association. Its source also documents that forcing Wi-Fi power-save off while Bluetooth is active can abort in the ESP32 Wi-Fi power-management path.

V15 therefore removes only that association-time `WiFi.setSleep(false)` call.

No reconnect timing, BSSID selection, scanning, routing, LoRa, chat, UI, encryption or radio-profile behavior is changed from V13.

## Validation

CI applies V11, V12 and V13 first, then applies this one-line behavioral fix, runs contracts and builds the real `LilyGo_TDeck_companion_radio_touch` target.

The resulting release is V15 RC1 until the affected Android hotspot has been physically retested on the T-Deck Plus.
