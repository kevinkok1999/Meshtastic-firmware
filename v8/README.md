# MeshOffGridNL Version 8 — pre-code specification

Status: CODING-READY PREPARATION
Base: validated Version 7 commit 4413758a25dfbc7958d134bbe9b1734243d608fa
Prepared: 2026-09-20

## Goal

Version 8 extends the working V7 T-Deck Plus firmware into a transport-independent, range-first multi-radio system without changing or overwriting V1-V7.

The normal chat stays the single user interface. Route selection happens underneath it.

Supported/targeted transport set:

1. Existing MeshCore over onboard SX1262 LoRa — mandatory legacy-compatible path.
2. Existing V7 ESP-NOW Long Range — built-in fast/direct path.
3. Digi XBee XR 868 — optional external independent 868 MHz DigiMesh path.
4. Semtech LR2021 LoRa Gen 4 — optional external advanced radio path.

The external radios are optional. A T-Deck without them must boot and operate normally using LoRa + ESP-NOW LR.

## Key design decisions

- V7 is immutable. V8 starts from the validated V7 release commit.
- One firmware image should support multiple hardware profiles where practical.
- No XBee/LR2021 hardware is assumed present; drivers must fail closed and leave stock operation intact.
- Public/channel traffic remains on the normal MeshCore/LoRa path in the first V8 coding milestone.
- Alternate transports are introduced first for direct messages and transport-control frames.
- All alternate transports use end-to-end protected V8 payloads. No plaintext-DM shortcut is allowed.
- A shared message ID is used across all bearers so fallback/redundant delivery does not duplicate chat messages.
- A sub-GHz RF arbiter prevents uncontrolled simultaneous transmissions between onboard SX1262, XBee XR 868 and LR2021.
- Route selection is metric-driven rather than a hard-coded "ESP-NOW then LoRa" chain.
- One GitHub firmware runner remains the only build lane; analysis, docs and host-side tests can proceed without consuming it.

## Hardware profiles

- STOCK: onboard SX1262 + ESP32-S3 ESP-NOW LR.
- XBEE: STOCK + external Digi XBee XR 868.
- LR2021: STOCK + external Semtech LR2021.
- FULL: STOCK + XBee XR 868 + LR2021.

Until a safe expansion-board pinout is verified, XBee/LR2021 pin assignments stay runtime/configurable and no GPIO is guessed.

## Not in V8 core

Wi-Fi HaLow, satellite and backscatter are not part of the first V8 implementation. They remain future transport candidates behind the same interface. They are excluded now because they add hardware/service complexity without improving the first T-Deck coding milestone enough to justify the risk.
