# P1 Pro V8 <-> T-Deck V19 hardware acceptance

V8 may be published as an RC after CI/build succeeds. Do not mark Stable until these physical gates pass.

## A — V8 installer / recovery
- Run the V8 All-in-One clean install on a real P1/P1 Pro.
- Erase completes, the known P1 bootloader returns, and the V8 V19 BLE ZIP is flashed.
- If serial re-enumeration is not granted by the browser, the SENSECAP UF2 rescue path restores the same V8 image.
- Bootloader 0.9.2 OTAFIX2.2 remains intact.

## B — Bluetooth companion
- P1 appears in the MeshCore app after boot.
- BLE pairing succeeds.
- Device info and contacts/channels load.
- Attempting another radio profile from the app is rejected or immediately remains on the V19 profile.

## C — exact V19 RF profile
- P1 reports/uses 869.618 MHz / 62.5 kHz / SF8 / CR5.
- Default TX is 22 dBm and values above 22 dBm cannot be applied.

## D — bidirectional T-Deck V19 link
- T-Deck V19 -> P1 V8 receives a public-channel message.
- P1 V8 -> T-Deck V19 receives a public-channel message.
- Repeat with a shared private channel/key.
- Record RSSI/SNR at a short known-good test distance before range testing.

## Stable gate
Only after A-D pass on real hardware may the V8 V19 BLE release be called Stable.
