# MeshOffGridNL V13 — Mobile-style Wi-Fi reliability

V13 builds on V11 encrypted worldwide direct messages and the V12 scrollable Wi-Fi list.

The main goal is simple: a T-Deck should connect to a normal 2.4 GHz home network or Android hotspot and stay connected without the settings screen or a background retry loop fighting the association.

## V13 Wi-Fi architecture

V13 keeps WadaMesh beta_83 as the upstream base but adds a narrow T-Deck-only association layer.

Key changes:

- **One reconnect owner.** Arduino background auto-reconnect is disabled for V13 T-Deck builds; the existing main loop is the single place that starts/retries associations.
- **No erase-on-retry.** The old recovery path called `WiFi.disconnect(false, true)`, erasing the driver's AP configuration before retrying. V13 uses `WiFi.disconnect(false, false)` and keeps the station state intact.
- **Selected AP handoff.** A scan now retains the selected AP's BSSID, channel and authentication mode in RAM. Tapping a network can therefore connect to the exact AP that was shown instead of immediately performing another blind fast scan by SSID.
- **Full-channel fallback.** When there is no fresh AP hint (for example after a reboot or for a hidden network), the station uses an all-channel scan policy before association.
- **Radio awake during authentication.** Power saving is disabled while the WPA/WPA2/WPA3 handshake is in progress. The existing base firmware re-enables modem sleep after a successful connection.
- **Less retry churn.** Retry spacing is increased from 10 to 15 seconds so a slower phone-hotspot authentication is not repeatedly reset.
- **Opening Wi-Fi settings no longer drops a healthy link.** V12 automatically kicked a fresh scan when the page opened; on ESP32-S3 that could deliberately disassociate first. V13 only auto-scans when the device is not already connected. Manual "Scan again" remains available.
- **Useful failure text.** Common ESP32 disconnect reasons are surfaced as hotspot-not-found, authentication failure, association failure, handshake timeout or beacon timeout, including the numeric reason code.

## Android hotspot focus

The ESP32-S3 radio remains **2.4 GHz only**. V13 does not pretend to add 5 GHz support.

For a visible Android hotspot, V13 improves the part after discovery:

1. retain the exact scanned AP;
2. select its BSSID/channel;
3. start one controlled association;
4. keep the radio awake during authentication;
5. keep AP configuration intact across retries;
6. report the actual failure reason if the connection still does not succeed.

This makes hardware testing actionable: if an individual Android model still refuses to join, the on-device reason code identifies whether the remaining issue is AP discovery, WPA authentication/SAE, association, or link stability.

## Existing MeshOffGridNL behaviour preserved

- V11 AES-256-GCM worldwide direct-message bridge remains enabled.
- Normal LoRa messaging remains the off-grid route.
- V11 LoRa/internet duplicate suppression remains enabled.
- V12 directly scrollable saved/scanned Wi-Fi list remains enabled.
- Radio profile remains unchanged: 869.618 MHz, 62.5 kHz, SF8, 22 dBm cap, DIO2 RF switch and boosted RX.

## Validation policy

A GitHub build passing proves the firmware compiles and packages correctly. It does **not** prove every phone hotspot works on physical hardware.

The V13 release is therefore published as an RC until it has been flashed onto a real T-Deck/T-Deck Plus and verified against the Android hotspot that previously failed.
