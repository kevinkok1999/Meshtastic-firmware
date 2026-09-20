# V8 Team Coordination

There is one firmware build lane. Specialists may work in parallel only on isolated analysis, interfaces and host-side tests.

## A — Transport architecture specialist
Owns:
- common transport contract
- message envelope
- message IDs
- delivery queue
- route scoring/hysteresis
- dedupe
- V7 compatibility seam

Must not rewrite MeshCore routing internals without evidence.

## B — ESP32-S3 / ESP-NOW specialist
Owns:
- existing V7 ESP-NOW behavior
- backward compatibility
- Wi-Fi LR lifecycle
- BLE/Wi-Fi coexistence
- ESP-NOW metrics

Must preserve LoRa-only behavior.

## C — XBee XR 868 specialist
Owns:
- API codec/parser
- UART lifecycle
- module queries
- 64-bit addressing
- NP/MTU discovery
- TX status
- XBee-specific tests

Must not guess GPIOs.

## D — LR2021 / RadioLib specialist
Owns:
- exact RadioLib pin/version validation
- LR2021 adapter
- SPI behavior
- EU868 profile
- standard-LoRa compatibility experiments
- advanced PHY experiments after baseline

Must not replace the onboard SX1262 in the first milestone.

## E — RF/power specialist
Owns:
- SubGhzRfArbiter
- coexistence measurements
- antenna-placement test plan
- current/power measurements
- hardware profile validation

Must block release if repeated RF self-interference makes routing unreliable.

## F — Saitama/MeshCore security specialist
Owns:
- identity binding
- protected-payload boundary
- signed capabilities
- application ACK semantics
- no-plaintext-DM rule
- legacy compatibility

## G — UI/config specialist
Owns:
- route-mode settings
- optional hardware settings
- diagnostics
- route label
- migration/defaults

Radio logic must not live in screens.

## H — QA/CI/installer specialist
Owns:
- host tests
- test matrix
- one-runner scheduling
- build/image verification
- release hashes
- installer preview
- proof V1-V7 remain unchanged

## Integration rules

1. Interfaces first.
2. Host tests before firmware builds.
3. One integration branch owns shared interfaces.
4. No parallel firmware builds.
5. Every interface change updates tests/docs in the same change.
6. STOCK profile regression failure blocks optional-radio work.
7. Optional-radio absence must always be a supported state.
8. Production website stays untouched until real-device QA passes.
