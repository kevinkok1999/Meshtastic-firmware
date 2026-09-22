# MeshOffGridNL V15 — Hotspot Stability

V15 is a focused stability release built from the V13 Mobile Wi-Fi line, not from V14.

The trigger for V15 is a real T-Deck Plus field failure: selecting an Android 2.4 GHz hotspot in V13 could associate far enough to enter the ESP32-S3 Wi-Fi power-management path and then reboot with a crash report.

## What V15 changes

- Keeps the proven V11 → V12 → V13 WadaMesh feature chain.
- Removes the V13 association-time `WiFi.setSleep(false)` override. WadaMesh already owns Wi-Fi/BLE coexistence and enables the correct modem-sleep state after association. V15 no longer fights that state machine during reconnects.
- Makes a scanned BSSID/channel hint **one-shot**. The exact AP is tried once; if it fails, the next retry falls back to an all-channel SSID association instead of repeatedly pinning a stale mobile-hotspot BSSID.
- Extends the controlled retry interval from 15 s to 20 s so WPA authentication is less likely to be torn down by a new attempt.
- Retains V13's single explicit reconnect owner, no `eraseap` retry, selected-AP handoff, full-channel fallback and on-device disconnect reason diagnostics.
- Retains V11 encrypted global chat and normal LoRa fallback.
- Does not import V14 DirectLink changes.

## Why the power-management fix matters

The pinned WadaMesh base explicitly warns that forcing Wi-Fi power-save off while a Bluetooth controller is active can abort in the ESP32 Wi-Fi power-management path. V13 forced `WiFi.setSleep(false)` on every association attempt. V15 removes that override and leaves coexistence policy with the base firmware.

## Validation

CI must pass the V11, V12, V13 and V15 contract tests and compile the real `LilyGo_TDeck_companion_radio_touch` target before a V15 RC asset is published.

A successful CI build proves compile/package integrity. Physical Android-hotspot validation on the affected T-Deck Plus is still required before V15 should be called stable.
