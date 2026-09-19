# V6 Single-Runner Execution Plan

## Goal
Build V6 in deterministic vertical slices with exactly one canonical GitHub Actions runner path.

This document is the execution contract for implementation.

## Core rule
At any moment there is exactly ONE canonical candidate commit allowed to consume the firmware runner.

No parallel firmware builds.
No competing release candidates.
No downstream artifact publication from stale commits.

## Branch model

Architecture:
- v6-architecture-prep — design only

Implementation:
- v6-dev — canonical implementation branch

Release:
- v6-candidate — generated/pinned candidate metadata only after all automated gates pass
- v6-release — only after hardware validation and explicit promotion

Existing V1–V5 branches remain untouched.

## Runner concurrency
Use one workflow concurrency group:

group: v6-firmware-<branch>
cancel-in-progress: true

Newer commits cancel stale queued/in-progress V6 firmware runs when safe.

Do not share this group with V1–V5 workflows.

## Pipeline order
The single runner workflow is sequential:

1. host/static checks
2. generated protocol/schema validation
3. unit tests
4. property/regression tests
5. fuzz regression corpus
6. simulator scenarios
7. ESP32-S3 package/toolchain setup
8. canonical firmware compile
9. partition/image validation
10. flash bundle creation
11. manifest/hash/provenance generation
12. flash-ready candidate gate
13. artifact publication

A later stage NEVER runs if an earlier stage failed.

## Runner conservation
Do as much work as possible before consuming the hardware/toolchain stage.

Fast host checks should catch:
- syntax
- schema
- pure C++ logic
- wire encode/decode
- route scoring
- storage model
- state machines
- privacy policy
- test vectors

Only after these are green should the workflow install/build the full ESP32-S3 toolchain.

## Commit discipline
Do not push five speculative fixes while a canonical runner is compiling.

Workflow:
1. inspect current failure;
2. determine root cause;
3. batch related fixes;
4. run every possible local/host check;
5. push ONE corrected commit;
6. let the single canonical runner evaluate it.

## Vertical slices

### Slice 0 — Scaffold
No behavior change.
Add:
- src/v6/ directories
- interfaces
- compile guards
- host-test target
- no T-Deck runtime activation

Runner requirement:
host checks + existing V5 production target still compiles.

### Slice 1 — Message Identity + Wire Codec
Implement:
- 128-bit MessageId
- V6 wire version
- bounded envelope codec
- parser limits
- deterministic test vectors
- fuzz target skeleton

No radio behavior yet.

Exit gate:
- encode/decode roundtrip
- malformed/truncated reject
- bounds tests
- no heap-unbounded parser behavior
- V5 behavior unchanged

### Slice 2 — Delivery State Machine
Implement:
CREATED -> PROTECTED -> QUEUED -> TRANSPORT_ACCEPTED -> DELIVERED / EXPIRED

No crypto secrets yet; test provider allowed.

Exit gate:
- duplicate events idempotent
- expiry monotonic
- state cannot regress
- explicit distinction transport accepted vs delivered

### Slice 3 — Transactional MessageStore
Implement:
- versioned records
- bounded queues
- journal/atomic commit model
- power-loss simulator

Exit gate:
- reset at every injected commit boundary yields old-valid or new-valid state
- no key/message state half-commit

### Slice 4 — CryptoProvider Boundary
Implement interface + test adapter first.
Then selected reviewed crypto implementation.

Exit gate:
- official/test vectors
- corrupted authentication rejects
- replay state tests
- no key material reaches transport API

### Slice 5 — One V6 Native Direct Transport
Use ONE existing physical path first, preferably LoRa adapter.

Do not add ESP-NOW/XBee/Internet simultaneously.

Exit gate:
two dev T-Decks exchange one V6-native encrypted text message.

### Slice 6 — Receipts + Dedup
Implement end-to-end delivery receipt semantics.

Exit gate:
- duplicates never create duplicate chat entry
- transport acceptance never displays as delivery

### Slice 7 — Store-Carry-Forward
Implement opaque custody + expiry + bounded copies.

Exit gate:
simulator demonstrates delivery across disconnected time windows.

### Slice 8 — Off-Grid Privacy
Implement approved subset:
- rotating discovery IDs
- pairwise/scoped pseudonyms
- sender-sealed header
- padding classes
- privacy-policy tests

Exit gate:
required privacy profile does not silently downgrade.

### Slice 9 — ESP-NOW + XBee Adapters
Map existing proven transports into frozen Transport API.

Exit gate:
same V6 envelope semantics across all off-grid transports.

### Slice 10 — Blind Internet Relay
Separate Rust repository/service.
Firmware gets InternetTransport adapter.

Exit gate:
relay cannot decrypt application message content.

### Slice 11 — Privacy Tunnel / Split-Knowledge Path
Start with application-scoped protected path unless benchmark proves full IP VPN superior.

Exit gate:
TUNNEL_REQUIRED leak tests pass.

### Slice 12 — UX Integration
Only after message semantics are stable.

Exit gate:
core NL flows work without protocol jargon.

### Slice 13 — Groups
Only after direct-message reliability/security stabilizes.

### Slice 14 — Secure Production
Signed updates, A/B rollback, Secure Boot, Flash Encryption and provisioning.

Never do irreversible eFuse work earlier.

## Decision gates by slice

Not required to start Slice 0–1:
- final MLS implementation
- full VPN vs application tunnel
- post-quantum suite
- production eFuse layout
- federation/multi-relay
- FEC

Required before Slice 4:
- direct-message crypto implementation
- identity key type
- contact verification encoding
- key-change policy

Required before Slice 5:
- Transport API frozen
- LoRa adapter mapping
- V6 native envelope size limits

Required before Slice 8:
- off-grid privacy metadata budget
- rotating identifier construction
- padding classes

Required before Slice 10:
- blind relay mailbox model
- relay API
- retention policy

Required before Slice 11:
- tunnel architecture choice for V6.0
- fail-closed policy
- DNS/bootstrap policy

Required before Slice 14:
- signing key custody
- rollback policy
- Secure Boot/Flash Encryption plan
- recovery path
- provisioning dry run

## Artifact naming
Every candidate artifact records:
- V6 protocol version
- source commit
- Actions run ID
- environment
- SHA-256
- partition map
- toolchain version
- test summary

Never publish mutable latest.bin as the only durable identity of a release.

## Definition of Flash-Ready
A V6 build is flash-ready only when:
- all automated gates green;
- exact ESP32-S3 target compiled;
- full image validated;
- hash generated;
- manifest pinned;
- no placeholder values;
- installer code pins exact source/hash;
- preview deployment READY;
- physical hardware test still explicitly marked pending until performed.

## Definition of Stable
Stable requires:
- automated flash-ready gates;
- physical T-Deck boot test;
- send/receive test;
- reboot persistence test;
- recovery test;
- privacy policy test;
- battery/network smoke test;
- no unresolved critical security defect.

## One-runner operating rule
The runner is a verification resource, not a debugging console.

We debug with:
- source inspection
- host tests
- static checks
- simulation
- targeted documentation/research

Then spend the runner only on the best current candidate.
