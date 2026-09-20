# MeshOffGridNL Version 7 — Saitama Hybrid Transport

Status: architecture/pre-implementation handoff. No V7 firmware binary is released from this branch yet.

## Goal
Version 7 is a GPL-3.0-or-later derivative of Saitama v1.3.0 for LilyGo T-Deck Plus. It keeps standard MeshCore/SX1262 LoRa interoperability and adds an optional ESP-NOW Long Range direct transport on the ESP32-S3.

The user-facing objective is one messaging UI with multiple transports:
- AUTO (default): prefer a healthy V7 ESP-NOW LR direct path when available; fall back to standard MeshCore/LoRa.
- LORA_ONLY: behave as Saitama/MeshCore and remain interoperable with normal MeshCore nodes.
- ESPNOW_LR_ONLY: direct V7-to-V7 transport for diagnostics/testing and explicit user choice.
- REDUNDANT: optional reliability mode; duplicate delivery must be suppressed before messages reach the UI.

## Non-negotiable invariants
1. Do not modify or rewrite historical V1/V2/V3/V4/V5/V6 branches or release artifacts.
2. V4 remains the unmodified official Saitama v1.3.0 image already exposed by MeshOffGridNL.
3. MeshCore protocol compatibility over LoRa must remain intact.
4. Do not weaken message confidentiality to make ESP-NOW work. No plaintext private-chat payload may be broadcast over ESP-NOW.
5. MeshCore is a pinned submodule. Prefer Saitama-side adapters/hooks; modify/fork MeshCore only if a clean injection/serialization API is impossible and document the exact reason.
6. T-Deck Plus target remains 16 MB flash, DIO, ESP32-S3, with the Saitama boot/UI/BLE initialization constraints preserved.
7. No production website publication until the V7 merged image passes the release gates in RELEASE_CHECKLIST.md.
8. One GitHub runner is available: keep firmware build CI serial and single-target.

## Source of truth
See UPSTREAM.lock.json. V7 starts from Saitama tag v1.3.0 and its exact MeshCore submodule revision.

## Coding entry point
Follow CODEX_HANDOFF.md and IMPLEMENTATION_PLAN.md. ARCHITECTURE.md defines the boundaries. TEST_MATRIX.md is the acceptance contract.

## Website release
Only after a validated binary exists:
- create immutable V7 release artifacts,
- record exact byte size and SHA-256,
- create a V7 installer API route,
- add V7 to the installer selector on NL/EN/DE,
- validate in a Vercel preview,
- only then promote/merge to production.
