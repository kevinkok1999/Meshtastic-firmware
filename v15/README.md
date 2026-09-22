# MeshOffGridNL V15 — V13 Hotspot Crash Fix

V15 is an exact continuation of the V13 Mobile Wi-Fi firmware. It is not based on V14.

The only functional change from V13 is the fix for the crash seen after connecting the T-Deck Plus to the Android 2.4 GHz hotspot.

## V15 = V13 + one crash fix

V15 keeps the complete V13 behavior unchanged:

- V13 Wi-Fi reconnect ownership stays unchanged.
- V13 selected BSSID/channel handoff stays unchanged.
- V13 all-channel fallback stays unchanged.
- V13 retry interval stays 15 seconds.
- V13 no-erase reconnect behavior stays unchanged.
- V13 on-device disconnect diagnostics stay unchanged.
- V11 encrypted worldwide chat stays unchanged.
- LoRa fallback and radio settings stay unchanged.
- No V14 DirectLink code is imported.

The single functional fix is:

- remove the V13 association-time `WiFi.setSleep(false)` override from `v13WifiBegin()`.

The WadaMesh base already owns the Wi-Fi/Bluetooth coexistence power policy. V15 therefore stops overriding that policy during hotspot association/reconnect.

## Validation

CI applies V11, V12 and V13 first, then applies this one-line-behavior V15 patch. Contract tests verify that the V13 behavior is preserved and only the unsafe association-time sleep override is absent.

The real `LilyGo_TDeck_companion_radio_touch` target must compile and package successfully before the V15 RC release is published.

Physical retesting with the Android hotspot that triggered the V13 crash is still required before calling V15 stable.
