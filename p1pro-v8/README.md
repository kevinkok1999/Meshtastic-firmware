# MeshOffGridNL P1 Pro V8 — V19 BLE Companion

V8 keeps the official MeshCore **SenseCap_Solar_companion_radio_ble** role and adds a strict
radio compatibility contract for MeshOffGridNL T-Deck V19.

## Pinned upstream

- MeshCore: `d92964352441e53b93e8667b802e04f6e072b39e` (Companion v1.17.1)
- Target: `SenseCap_Solar_companion_radio_ble`
- Hardware: Seeed Studio SenseCAP Solar Node P1 / P1 Pro
- BLE stays enabled with the upstream PIN default 123456.

## V19 radio contract

- Frequency: **869.618 MHz**
- Bandwidth: **62.5 kHz**
- Spreading factor: **SF8**
- Coding rate: **CR5**
- TX power: default 22 dBm, hard ceiling 22 dBm

The profile is applied to defaults and re-applied after persisted preferences are loaded.
`CMD_SET_RADIO_PARAMS` only accepts the same V19 profile; incompatible app-side profile
changes return an illegal-argument response. `CMD_SET_RADIO_TX_POWER` remains adjustable
only inside the P1 range and cannot exceed 22 dBm.

## Why this is separate from P1 V2

P1 V2 is an autonomous `SenseCap_Solar_repeater`. V8 deliberately remains a
`SenseCap_Solar_companion_radio_ble` so the P1 stays visible to the MeshCore app over
Bluetooth while using the T-Deck V19 radio profile.

## Release status

Software build/contracts can validate the profile and generated image, but **Stable** still
requires the physical P1 Pro <-> T-Deck V19 acceptance test.
