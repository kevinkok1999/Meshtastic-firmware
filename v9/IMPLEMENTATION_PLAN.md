# V9 — Three-Phase Pre-Code Plan

## Phase 1 — Audit and freeze

Status: COMPLETE.

Actions:
- V8 firmware head verified at fbe6917df3f040ab72dc3dee8b82c131cbc8fea5.
- Created v9-architecture-prep directly from V8.
- V1-V8 remain immutable.
- V8 current reality documented: stock V8 has LoRa + ESP-NOW adaptive routing; XBee/LR2021 are optional code seams, not automatically active hardware.
- Current radio capabilities reviewed.
- V9 scope frozen around delivery/range engineering instead of adding another core radio.

Gate passed:
V9 has a stable base and a bounded feature set.

## Phase 2 — Freeze protocol and interfaces

Status: COMPLETE in specification.

Interfaces to implement:
1. ReachEngine
2. PathManager
3. LinkMetricStore
4. V9EnvelopeCodec
5. FragmentWindow
6. SelectiveAckCodec
7. FecCodec
8. StoreForwardQueue
9. RelayPolicy
10. PhyProfileManager
11. SubGhzRfArbiter integration
12. V8CompatibilityAdapter

Key rule:
Each component must be host-testable against mocks before firmware integration.

Gate passed:
Radio-specific drivers can be developed without embedding policy in UI or MeshService.

## Phase 3 — Coding/QA/release handoff

Status: COMPLETE in specification.

Coding sequence:

A. Host-only core
- MessageId128
- envelope parser/serializer
- dedupe/replay window
- fragment window
- selective ACK bitmap
- simple erasure-FEC codec
- route/path score
- hysteresis
- store-forward queue model

B. Compatibility
- V8 envelope/route adapter
- V7 compatibility regression
- legacy MeshCore fallback

C. Built-in hardware
- onboard LoRa metrics adapter
- ESP-NOW LR metrics adapter
- recovery ladder
- multipath policy

D. Optional radios
- XBee XR 868 metrics + profile adapter
- LR2021 profile adapter
- SubGhzRfArbiter integration

E. Persistence
- bounded LittleFS journal
- recovery after interrupted write
- wear/rate limiting
- expiry cleanup

F. UI
- normal chat unchanged
- delivery state
- route label
- diagnostics
- range mode settings

G. Validation
- native tests first
- exactly one T-Deck build per integration checkpoint
- stock hardware regression first
- then two-device V9 tests
- then optional radio hardware tests

H. Release
- V9 asset branch only after build validation
- dedicated v9-installer-preview branch in MeshOffGridNL
- immutable commit URL + byte size + SHA-256
- Vercel preview
- real T-Deck flash test
- production promotion after hardware QA

## Single-runner rule

Use:
- concurrency group v9-firmware
- cancel-in-progress true
- only t-deck target for release candidate
- no firmware build for documentation-only commits
- host-native tests before consuming the firmware build lane

## Milestone 1 — V9 STOCK

Definition of done:
- boots on stock T-Deck Plus;
- V8/V7/legacy compatibility preserved;
- selective fragment retransmission works;
- path scoring/fallback works;
- store-forward disabled/empty state cannot break chat;
- no external module required.

## Milestone 2 — V9 Relay/Store

Definition of done:
- two-hop protected V9 forwarding works;
- queued protected message survives reboot when persistence enabled;
- expiry works;
- relay loop prevented;
- duplicate arrival delivered once;
- bounded storage verified.

## Milestone 3 — V9 Ultra Hardware

Definition of done:
- XBee external profile tested;
- LR2021 external profile tested;
- SubGHz coexistence matrix completed;
- adaptive profile selection based on measured data;
- no regression on stock profile.
