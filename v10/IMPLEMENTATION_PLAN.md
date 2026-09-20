# V10 Three-Phase Plan

## Phase 1 — Audit and freeze

Status: COMPLETE.

- V10 base fixed at V9 source d24bd9910b53589652778ff79fc7da795d790c07.
- V1-V9 remain immutable.
- Target narrowed to exactly two standard T-Deck Plus devices.
- No hardware modification.
- Five logical bearers mapped to two physical RF resources.
- Successful V9 toolchain versions recorded.
- Current RadioLib SX1262 GFSK support verified.
- ESP32-S3 Wi-Fi LR and BLE Coded PHY feasibility recorded.
- Production website intentionally untouched.

Gate:
No new bearer may be implemented without a rollback path to a V9-compatible home mode.

## Phase 2 — Architecture and contracts

Status: COMPLETE in specification.

Frozen components:
1. PairSession
2. DirectLinkFrameCodec
3. MessageId128
4. SelectiveAckWindow
5. FecPolicy
6. LinkMetricStore
7. ProbePlanner
8. DirectLinkBrain
9. RFResourceScheduler
10. LoRaCompatBearer
11. GfskDirectBearer
12. EspNowLrBearer
13. WifiLrDirectBearer
14. BleCodedBearer
15. DeliveryCoordinator

Gate:
Core policy compiles in host tests without Arduino/radio hardware.

## Phase 3 — Coding handoff

Status: COMPLETE in specification.

### Milestone A — safe core
- create v10-dev;
- reuse/test MessageId128 + selective ACK + XOR FEC;
- implement PairSession;
- implement DirectLinkBrain;
- implement RFResourceScheduler mock;
- host tests.

No firmware runner yet.

### Milestone B — preserve known routes
- integrate V9 LoRa;
- integrate current ESP-NOW LR;
- change cross-route dedupe to MessageId128;
- two-device V10-over-existing-bearers test;
- one T-Deck CI build.

### Milestone C — GFSK proof
- add exact RadioLib pin;
- implement modem transition guard;
- implement GFSK probe/data adapter;
- host state-machine tests;
- one T-Deck build;
- physical repeated LoRa<->GFSK cycle test.

### Milestone D — Wi-Fi LR proof
- deterministic AP/STA role;
- authenticated direct UDP session;
- mode restore to ESP-NOW;
- two-device physical range/transition test.

### Milestone E — BLE Coded proof
- compile-time stack capability probe;
- exact API adapter for bundled ESP-IDF;
- Coded PHY S2/S8 negotiation;
- coexistence scheduling;
- two-device physical test.

### Milestone F — optimization
- measured route scores;
- transition-cost model;
- probe economy;
- adaptive fragments/FEC;
- bounded recovery ladder.

### Milestone G — release
- native tests;
- exactly one release T-Deck build;
- image validation;
- immutable v10-assets;
- SHA-256 + byte size;
- separate website preview;
- real A/B flash test;
- production only after successful two-device QA.

## Single-runner rule

- documentation changes: zero firmware runs;
- batch source changes;
- host tests before push;
- one integration/release build at a time;
- concurrency group v10-firmware;
- cancel-in-progress true.
