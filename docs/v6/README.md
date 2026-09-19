# MeshOffGridNL V6 — Pre-Coding Architecture

Status: DESIGN ONLY. No V6 production firmware is implemented on this branch.

V6 is a new communication architecture above the proven T-Deck/Meshtastic hardware stack. The design goal is a privacy-first, offline-first communicator that remains simple for normal users.

## Non-negotiable product principles
1. User chooses Off-grid, Internet, or optional Smart mode. Never silently violate that choice.
2. Plaintext exists only at endpoints.
3. Relays and internet services handle opaque encrypted objects.
4. No phone number, email account, analytics ID, or GPS is required for core messaging.
5. V6-native messaging is transport-independent.
6. Existing V1–V5 firmware stays untouched.
7. Security-critical algorithms are standards-based; do not invent cryptographic primitives.
8. Developer devices remain recoverable; irreversible production hardening is a separate provisioning phase.
9. Every critical state machine must be testable without physical hardware.
10. The normal UI exposes concepts, not protocol jargon.

## Planned technology split
- T-Deck firmware: modern C++ on the existing ESP32-S3 / FreeRTOS / Meshtastic-compatible base.
- Wire definitions: protobuf/nanopb-compatible schemas with explicit versioning.
- Blind internet relay: Rust service, separate repository.
- Website/installer: TypeScript/JavaScript.
- Crypto: audited/standardized primitives behind a crypto-provider interface.

## Documents
- 01_ARCHITECTURE.md — component boundaries and ownership.
- 02_THREAT_MODEL.md — what V6 protects and what it cannot promise.
- 03_PROTOCOL_WIRE.md — V6 secure envelope and versioning.
- 04_CRYPTO_IDENTITY.md — identity, sessions, groups, key lifecycle.
- 05_TRANSPORT_ROUTING.md — LoRa/ESP-NOW/XBee/Internet abstraction and delivery.
- 06_STORAGE_RECOVERY.md — encrypted storage, queues, crash/power-loss recovery.
- 07_UX_PRODUCT_SPEC.md — user flows and progressive disclosure.
- 08_TEST_RELEASE_GATES.md — simulation, fuzzing, hardware QA and release gates.
- 09_BLIND_RELAY.md — server architecture and data minimization.
- 10_PRE_CODING_GATE.md — decisions that must be closed before implementation begins.
- 11_PRIVACY_TUNNEL.md — optional fail-closed privacy tunnel / internal VPN architecture.

- 13_OFFGRID_PRIVACY_LAYER.md — privacy architecture for LoRa/ESP-NOW/XBee/store-forward radio paths.
