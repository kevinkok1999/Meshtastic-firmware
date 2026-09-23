# MeshOffGridNL P1 Pro V1 — Hardware Acceptance

Release line: **P1 Pro V1 RC1**
Target: **Seeed Studio SenseCAP Solar Node P1 / P1 Pro**
Role: **MeshCore repeater / MeshOffGridNL base station**
T-Deck compatibility target: **MeshOffGridNL T-Deck V19**

RC1 may be published for controlled hardware validation. It MUST NOT be promoted to Stable until the physical gates below are completed.

## Gate A — install and boot
- Erase package flashes successfully through the nRF52 DFU route.
- RC1 DFU ZIP flashes successfully.
- UF2 fallback can be installed through the bootloader drive.
- Device boots without a reset loop.
- Serial CLI responds.
- `ver`, `board`, `stats-core`, `stats-radio`, `stats-packets` respond.
- `base health` responds and reports sane packet-pool values.
- After a cold power cycle, the node returns without manual intervention.

## Gate B — V19 radio compatibility
Use the exact radio profile currently used by the proven T-Deck V19 network.

- T-Deck V19 can hear/discover the P1 Pro V1 repeater.
- P1 Pro V1 can receive traffic originating from T-Deck V19.
- T-Deck A -> P1 -> T-Deck B succeeds.
- T-Deck B -> P1 -> T-Deck A succeeds.
- Direct T-Deck A <-> T-Deck B still works when the P1 is powered off.
- Rebooting the P1 does not require re-flashing or re-provisioning.

## Gate C — resilience
- Periodic adverts back off under queue pressure.
- TX/RX drops are counted instead of causing unbounded allocation.
- The fixed 32-packet pool does not grow.
- A transient full queue does not reboot the station.
- A hard pool stall only triggers recovery after the 120-second no-progress gate.
- Preferences survive repeated saves and power cycles.
- An interrupted preference replacement can recover from backup.
- GPS off/failure does not stop LoRa forwarding.
- Power-saving does not prevent expected LoRa wake/forward behaviour.
- No unexplained reset loop during soak testing.

## Gate D — routing limits
Software contracts verify:
- scoped flood maximum: 48
- unscoped flood maximum: 6
- advert flood maximum: 8
- one-byte path mode

Physical testing should increase route depth with available nodes/simulated traffic and verify that longer paths do not create growing queues, loops or lockups.

## Gate E — installer and recovery
- RC1 manifest SHA-256 matches every artifact.
- Erase artifact matches its manifest hash.
- Direct DFU install works on supported desktop Chromium browsers.
- UF2 fallback is documented and tested.
- Interrupted flashing is recoverable via DFU/bootloader mode.
- MeshOffGridNL V1 does not automatically upgrade the bootloader.

## Stable promotion
Only after Gates A-E pass:
- website label becomes **P1 Pro V1 Stable**
- GitHub release can stop being prerelease
- P1 V1 becomes the recommended stable P1 firmware
