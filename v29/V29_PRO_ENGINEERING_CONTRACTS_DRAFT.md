# V29 PRO — PRE-CODING ENGINEERING CONTRACTS / DRAFT 01
Date: 2026-10-20. Status: DRAFT; S1–S5 reviews and hardware measurements pending. No firmware edits in this document.

## 1. Source facts and architecture decisions
Existing V29 master spec describes an emergency fabric with v2 header, encrypted/signed store-carry-forward, P0–P3, SPIFFS snapshots and optional local portal. The V29 contract script checks symbols, but not behavior. The workflow runs V11→V29 overlays and tests on a pinned WadaMesh beta_83 upstream.
Decision ADR-001: keep V28 native chat as canonical chat UX, message database and RF-first send. V29 emergency fabric is a separately identified capability-gated emergency payload, not a second ordinary chat engine.
ADR-002: emergency priority cannot bypass EU868 regulatory limits; confirm radio profile EIRP/duty-cycle against actual band plan and antenna gain before RF changes.
ADR-003: receipt vocabulary QUEUED_LOCAL, RF_TRANSMITTED, RELAY_ACCEPTED, RECIPIENT_ACK, FAILED_EXPIRED. Never show DELIVERED merely on successful RF transmit.
ADR-004: no SD, internet, BLE or GPS dependency for core LoRa messaging.
ADR-005: 192h is external-power engineering target, not proven runtime.

## 2. Typed interface proposals (NOT yet code APIs)
### MessageRecord
id: 16-byte unique opaque ID; origin: verified public identity; recipient: opaque ID/group; created: monotonic-local order + optional wall-clock with provenance; ttl_remaining: bounded; priority: P0..P3; ciphertext: bounded bytes; authentication: validated origin signature and AEAD tag; transport_receipts: append-only event log. Mutable relay metadata cannot be treated as authenticated original metadata.
Failure: invalid signature, duplicate, expired, unsupported capability or queue full => explicit reason; never silently accept.

### TransportAdapter
capabilities() -> supported payload and mtu; state() -> DISCONNECTED/READY/DEGRADED; enqueue(record) -> ACCEPTED/REJECTED(reason); poll() -> bounded events; stop() -> release resources. One scheduler owns retries and airtime. RF transport remains independently usable.

### PortalSession
issue() -> short-lived scoped token; validate(token,origin,action) -> allow/deny; submit(action,recipient) -> bounded emergency queue. No GET side effects. No private key exposure. Rate limit per session and globally. No assurance of contacting 112.

### RadioMetrics
sample_timestamp, profile, RSSI/SNR if provided by driver, TX/RX counters, CAD when supported, confidence and sample source. Never label as spectrum analysis or SWR.

### UIEvent
typed event, payload, origin, timestamp, priority; bounded queue; render thread never waits for network. All screens define empty/loading/error/offline/back/focus states.

### PersistentStore
atomic_save(schema_version, blob) -> status; recover() -> last valid snapshot; quota() -> bytes; erase_scope(scope) -> explicit user confirmation. SD absence leaves critical config/journal intact.

## 3. Test acceptance matrix
T-MSG-001 same logical ID via RF+relay yields one user-visible message.
T-MSG-002 relay acceptance never maps to recipient delivery.
T-MSG-003 power cut during journal update restores latest valid committed state, with corruption flagged.
T-MSG-004 bounded queue rejects overflow predictably; P0 cannot starve all P1/P2 indefinitely.
T-RF-001 P1 Pro V8 baseline communication unchanged.
T-RF-002 airtime/power stay within configured legal profile; no auto-boost.
T-NET-001 RF messaging continues with router/relay/phone/SD absent.
T-NET-002 WiFi reconnection and portal AP cannot race for radio ownership.
T-SEC-001 invalid signature/tag/replay is rejected and counted without sensitive log contents.
T-SEC-002 expired/reused portal token and GET action attempts fail.
T-UX-001 every primary CTA reachable and reversible by touch, trackball and keyboard.
T-UX-002 emergency wording never implies 112 or NL-Alert dispatch.
T-SYS-001 low heap sheds caches before durable queues; no unbounded allocation.
T-SYS-002 microSD removal cannot corrupt essential messages.
T-REL-001 clean build from pinned upstream and overlays with SHA256 artifact and size report.
T-REL-002 all regression contracts + independent behavior tests pass before release.
All numerical latency/heap/energy thresholds remain UNSET pending baseline; never invent them.

## 4. Five-specialist handoff and signoff
S1: resource ownership map, partition/rollback feasibility, memory baseline, test T-SYS.
S2: transport scheduler, RF regulatory review, P1 interoperability, test T-RF/T-NET.
S3: cryptographic trust boundaries, mutable relay metadata integrity, replay/nonce model, test T-SEC.
S4: actual screen-by-screen 320x240 wireframes, Tropical assets, test T-UX.
S5: test harness, negative cases, CI runner policy and release evidence.
Each signs APPROVED/REJECTED/UNKNOWN with commit/test links. No role has signed yet.

## 5. Gate assessment
PC1: BLOCKED by divergent V28/V29 branch reconciliation, full workflow audit, baseline.
PC2: PARTIAL — interface proposals exist; real API mapping, budgets, wireframes, threat model and specialist reviews pending.
PC3: NOT READY — tickets require exact source paths, dependencies, tests, rollback and S1–S5 signoff.
