# MeshOffGridNL V27 — P1 Pro V8 Compatibility Contract

V27 MUST remain interoperable with the existing MeshOffGridNL P1 Pro V8 V19 BLE Companion release.

## Pinned P1 Pro V8 reference
- Release: p1pro-v8-v19-ble-rc1
- MeshCore base: companion v1.17.1
- Role: SenseCap_Solar_companion_radio_ble
- BLE retained
- Public group PSK retained

## Radio contract shared with the T-Deck V19→V26 line
- Frequency: 869.618 MHz
- Bandwidth: 62.5 kHz
- Spreading factor: SF8
- Coding rate: CR5
- TX power default: 22 dBm
- TX power ceiling: 22 dBm

V27's Internet transport is strictly additive. It MUST NOT retune or replace this RF path.

## User-experience contract
- P1 Pro V8 remains reachable over the existing radio path.
- A V27 T-Deck can continue local/off-grid messaging with P1 Pro V8 even when the global relay is unavailable.
- Internet state must not change the radio profile.
- Internet-originated messages are not automatically re-broadcast to P1/LoRa unless an explicit gateway mode is introduced later.
- One chat timeline remains the UI model; transport choice stays below the UI.

## Release gate
V27 is not Stable until bidirectional real-hardware tests pass:
1. T-Deck V27 -> P1 Pro V8 over RF.
2. P1 Pro V8 -> T-Deck V27 over RF.
3. Same tests with Wi-Fi connected on V27.
4. Same tests with Wi-Fi unavailable on V27.
5. Wi-Fi loss/recovery during an active chat does not change RF settings.
6. Global relay duplicates never create duplicate visible RF messages.

The P1 Pro V8 firmware itself is not modified by V27.
