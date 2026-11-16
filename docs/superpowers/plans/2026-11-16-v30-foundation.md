# V30 Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix verified emergency transport, persistence, security and local portal issues.
**Architecture:** Add a V30 overlay and host-testable C++ contracts; hook the pinned upstream transport at its actual dispatch and completion points, leaving V29 immutable. One owner manages queues and truthful state.
**Tech Stack:** C++/Arduino ESP32-S3, MeshCore, mbedTLS, PlatformIO, Python 3.12, host C++
**Spec:** `v30/MASTER_SPEC.md`

## Global Constraints
- Preserve V29 artifacts, native chat, P1 V8 baseline and existing flash partition offsets. Stock T-Deck Plus, no mandatory SD, Internet or phone.
- No production publication or unsupported claims; CI evidence is not HIL, RF compliance, runtime or commercial approval.
- Use fresh failing executable tests first; independently verify full applicable suite. Use exact baseline pins in spec.

## Review Focus
- RAW flood never dispatched; lower-layer dedup may reject retry.
- Queue pressure/power cut/old replay: no loss silently accepted and no false receipt.
- Malicious or unknown sender, manipulated relay age, reset nonce, expired TTL.
- AP session unauthorized requests, partial reads and previous power state.
- Legacy chat interoperability and P1 V8 remains independent.
---
### Task F1 — Baseline and packet routing
**Files:** Create `v30/apply_v30.py`, `v30/tests/test_transport.cpp`, `v30/tests/test_transport.py`, validation workflow `.github/workflows/v30_validation.yml`; modify only V30-patched upstream via overlay.
**Interfaces:** `enum class TxAdmission { Busy, NoPool, Accepted }`, `v30TryEnqueueEmergencyRaw(data,len,cookie,priority)`; completion callback `onV30TxResult(cookie,success,airtimeUs)`; V30 raw parser consumes its own magic only.
- [ ] Pin WadaMesh/dependency SHA and reconstruct V11→V29 baseline; record current app size and existing suites.
- [ ] Executable actual-core packet fixture test: RAW flood negative, zero-hop/direct positive; unrelated RAW/native DM/group unchanged, invalid M29 consumed; retries with same logical ID are received at application layer. Run `python v30/tests/test_transport.py`; RED expected on old flood route.
- [ ] Implement bounded direct adapter and explicit admission; connect logTx/logTxFail with stable cookie; separate outer attempt identifier from signed logical ID; avoid blanket priority 0.
- [ ] Run fixture and baseline V29/V28 suites; GREEN; compile for `LilyGo_TDeck_companion_radio_touch` with headroom guard. Commit.
### Task F2 — Durable inbox/outbox / acknowledgements
**Files:** Create `v30/overlay/src/helpers/esp32/V30Journal.{h,cpp}`, `V30Delivery.{h,cpp}`, executable tests `v30/tests/test_delivery.cpp`.
**Interfaces:** `admit(Bundle)->Admission`, `commit(bundle)->bool`, `recover()->RecoveryResult`, `receive(VerifiedEnvelope)->AckAction`, `applyReceipt(VerifiedReceipt)->bool`; states Rejected/Queued/Durable/TxAttempt/TxComplete/RecipientAck/Expired.
- [ ] Test bounded admission, 257 distinct messages + reboot replay, 9 critical incoming events, storage full, interrupted journal writes/headers/readback, latest corruption fallback, capacity reduction, false ACK from wrong sender, ACK replay, no duplicate UI. RED on legacy semantics.
- [ ] Implement journal A/B or append+compaction with commit validation. Persist origin+message ID before displaying; don't mark rejected messages seen. Quotas distinguish local/trusted/relay; critical inbox retained, explicit failure. Receipt signed by expected recipient; TX does not mean delivery.
- [ ] Run host C++ suite, contract tests and controlled power-cut storage simulation; commit.
### Task F3 — Crypto and trust
**Files:** Create `V30Envelope.{h,cpp}`, `V30ReplayGuard.{h,cpp}`, `v30/tests/test_envelope.cpp`; integrate delivery.
**Interfaces:** `verifyAndOpen(bytes,securityContext)->VerifyResult`, `reserveNonce(direction,peer)->NonceResult`, `isTrusted(fullPublicKey)->bool`.
- [ ] Test bad signature/tag, different recipient, malformed version/length/enums, TTL zero, expiry and age tamper, duplicate origin/ID, nonce reset, unknown critical sender, malformed ACK. RED before fix.
- [ ] Version wire protocol; signed immutable message fields, mutable relay hints untrusted; standard directional KDF/nonce reservation, no custom cipher; explicit no-clock limitations; full trust labels and anti-Sybil bounded admission.
- [ ] Run vectors + mutation/property tests plus baseline; commit.
### Task F4 — Portal
**Files:** Create `V30PortalSession.{h,cpp}`, `v30/tests/test_portal.cpp`; modify V30 portal overlay only.
**Interfaces:** `beginSession(physicalApproval)->Session`, `authorize(method,host,origin,token,intent)->Decision`, `poll(now)->SessionState`.
- [ ] Test GET may not actuate, invalid/duplicate token, host/origin rejection, unauthenticated AP peer, partial slow request, end/timeout restores prior Critical power mode, no secrets exposed; RED.
- [ ] Implement 128-bit per-session token outside URLs, physical trust confirmation, bounded parser/time/memory, explicit status and restore. Keep normal chat independent.
- [ ] Run actual parser tests and baseline; commit.
### Task F5 — Integrated validation
**Files:** `v30/qa/foundation-matrix.md`, `.github/workflows/v30_validation.yml`.
- [ ] Gate host tests, pin dependencies, serialized build; no release permission, no V29 publication workflow use.
- [ ] HIL two V30 units and three-node carry plus P1 V8 native public/private traffic; report actual TX/ACK/reboot evidence and exact build SHA. Mark BLOCKED when hardware unavailable.
- [ ] Verify no production upload, app partition fits and candidate artifacts labelled DEV. Commit evidence only after tests.
