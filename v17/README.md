# MeshOffGridNL V17 — Samsung S21 Hotspot Compatibility

V17 is a targeted compatibility release on top of V16 for the Samsung Galaxy S21 mobile hotspot.

## Why V16 can still fail on a Galaxy hotspot

The Galaxy hotspot can advertise WPA2, WPA3 or WPA2/WPA3 transition security. On ESP32-S3, WPA3 requires SAE plus Protected Management Frames (PMF). The V16 path still delegated security negotiation to Arduino `WiFi.begin()`, which does not explicitly configure the station's PMF/SAE policy for this hotspot.

## V17 connection path

For the T-Deck / T-Deck Plus target V17 bypasses Arduino's high-level association call and configures the ESP-IDF station directly:

- standard 2.4 GHz 802.11 b/g/n protocol mask;
- HT20 bandwidth for maximum hotspot compatibility;
- no BSSID or channel pinning;
- full-channel SSID association from the first attempt;
- WPA2 minimum security when a password is present, while allowing stronger WPA3 networks;
- PMF capable, but not required, so WPA2 and WPA3 transition networks both remain possible;
- SAE PWE = BOTH (Hunt-and-Peck + Hash-to-Element/H2E);
- two driver-level retries per connect request before the V16 higher-level retry policy takes over;
- V16 scan arbitration, single reconnect owner, crash fix and diagnostics remain active.

No Wi-Fi sleep/power-management override is reintroduced.

## Validation

CI must apply and contract-test V11, V12, V13, V15, V16 and V17, then build the real `LilyGo_TDeck_companion_radio_touch` target.

Physical testing on the user's Galaxy S21 remains the final validation step.
