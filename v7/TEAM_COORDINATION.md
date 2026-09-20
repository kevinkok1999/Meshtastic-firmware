# V7 Team Coordination

This document divides the coding work into specialist streams. Streams may work in parallel on analysis/tests/interfaces, but there is only one firmware build lane.

## A — ESP32-S3 / ESP-NOW specialist
Owns:
- Wi-Fi radio lifecycle
- WIFI_PROTOCOL_LR enablement
- ESP-NOW init/peer/channel state
- bounded TX/RX queues
- framing/fragmentation/reassembly
- ACK/retry metrics
- BLE coexistence measurements

Must not edit MeshCore crypto/routing internals.

## B — MeshCore / security specialist
Owns:
- locked MeshCore revision review
- protected-payload injection/serialization seam
- contact identity ↔ ESP-NOW peer binding
- dedup/message IDs
- backward compatibility

Must reject any design that sends decrypted private chat text as transport payload without end-to-end protection.

## C — Saitama UI/config specialist
Owns:
- append-only Config fields/migration
- route mode settings
- diagnostics
- peer V7 capability presentation
- no-regression UI behavior

Must not add radio logic directly into screens.

## D — QA/CI/release/installer specialist
Owns:
- unit/hardware test plan
- one-runner workflow
- artifact/image verification
- release hash/size manifest
- website V7 integration branch
- Vercel preview validation
- proof that V1-V6 remain unchanged

## Integration contract
1. Agree interfaces first.
2. A and B can implement behind mocks.
3. C consumes only coordinator APIs.
4. D runs static/unit checks freely, but schedules firmware build only after an integration checkpoint.
5. Every change that alters a shared interface updates tests in the same commit.
6. A failing LORA_ONLY regression blocks all feature expansion.
