# V8 — Three-Phase Pre-Code Plan

This file defines the work that must be complete before production coding starts.

## Phase 1 — Audit, preserve, choose architecture

Status: COMPLETE in v8-architecture-prep.

Outputs:
- V8 branch starts from validated V7 commit.
- V1-V7 remain untouched.
- Existing V7 ESP-NOW design reviewed.
- Existing XBee XR 868 scaffold reviewed.
- LR2021 selected as the advanced optional local radio.
- Wi-Fi HaLow/satellite/backscatter deferred from V8 core.
- Stock T-Deck operation defined as mandatory fallback.
- RF coexistence risk identified explicitly.

Gate:
No production firmware code is written until the architecture and hardware contracts are accepted.

## Phase 2 — Freeze interfaces, policy and protocol contracts

Status: COMPLETE in documentation; implementation starts only in coding phase.

Coding contracts:
1. Generalize HybridTransport into a V8 coordinator without breaking the existing V7 ESP-NOW wire behavior.
2. Add stable 64-bit message IDs shared across transports.
3. Add transport-neutral delivery queue.
4. Add metrics + scoring + hysteresis.
5. Add SubGhzRfArbiter.
6. Port XBee API codec/link logic into the Saitama overlay with unit tests.
7. Add LR2021 adapter through the existing RadioLib dependency.
8. Add signed V8 capability binding.
9. Keep public/channel traffic on normal LoRa for the first milestone.
10. Keep all external radios optional and auto-disabled if absent.

Gate:
The interfaces are frozen enough that each specialist can implement against mocks without changing chat/UI business logic.

## Phase 3 — Prepare coding/QA/release handoff

Status: COMPLETE in documentation.

Coding order:
A. host-testable message-ID, envelope, dedupe and route-policy core
B. ESP-NOW V7 compatibility adapter
C. XBee driver port + host codec tests
D. LR2021 adapter + compile-time hardware abstraction
E. RF arbiter
F. MeshService integration
G. settings/diagnostics UI
H. one-runner T-Deck build
I. two-device hardware tests
J. optional-radio hardware tests
K. installer preview branch
L. real flash from preview
M. production promotion only after successful device QA

Single-runner strategy:
- never run parallel firmware builds;
- use host/native tests before firmware build;
- concurrency group: v8-firmware;
- cancel-in-progress: true;
- build only t-deck target;
- generate app + merged binary once per integration checkpoint.

## First coding milestone definition of done

A V8 STOCK build is successful when:
- it flashes/boots on the T-Deck Plus;
- LoRa behavior is not regressed;
- V7 ESP-NOW peers still work;
- V8-to-V8 ESP-NOW direct chat works;
- message-ID dedupe works for repeated identical text;
- route manager can fall back to LoRa;
- external transports being absent causes no UI or boot failure.

## Second coding milestone definition of done

XBee milestone:
- two T-Decks with XR 868 hardware exchange a protected V8 DM;
- TX status feeds metrics;
- fallback to LoRa works;
- sub-GHz arbiter prevents simultaneous local TX;
- stock build behavior remains unchanged without XBee.

## Third coding milestone definition of done

LR2021 milestone:
- two compatible external LR2021 setups exchange the protected V8 envelope;
- exact EU868 profile is recorded;
- standard-LoRa compatibility with an SX1262 peer is tested rather than assumed;
- failure/unplugging does not crash or block chat;
- range/latency/energy measurements are recorded.

## Website release rule

Do not edit production installer during coding.

After firmware QA:
- create a dedicated V8 installer-preview branch in Meshoffgridnl;
- pin immutable V8 asset URL + SHA-256 + byte size;
- preserve V1-V7 selectors;
- Vercel preview first;
- real T-Deck flash test from preview;
- only then merge/promote.
