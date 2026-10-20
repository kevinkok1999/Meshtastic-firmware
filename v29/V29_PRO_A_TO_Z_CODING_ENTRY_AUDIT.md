# V29 PRO — A TO Z PRE-CODING AUDIT & IMPLEMENTATION ENTRY PACK
Date 2026-10-20. Status: audit evidence, NOT final signoff. One release V29 PRO.

## Executive verdict
Coding blueprint is substantive but **not fully validated**. Source inspections were performed on V29EmergencyFabric.h/.cpp, V29EmergencyPortal.h/.cpp, apply_v29.py, V29 contract script, CI workflow, and V28/V29 branch diff. No end-to-end build, runtime test or physical board measurement is evidenced by this document. Do not call the system fully code-ready.

## Verified facts
1. Upstream build pins WadaMesh commit 6fe6b9f06332708b6ed0cc78958017d0dcc9e35d; applies V11→V29 patches.
2. Emergency wire cap 184 bytes, 82-byte header, 16-byte AEAD tag, 64-byte signature, 22-byte body. V29 queue cap 512, seen cap 256, event cap 8; TTL constant 192 hours.
3. Existing V29 uses SPIFFS two-snapshot recovery with generation/checksum, not proven power-loss-safe without fault injection.
4. V29EmergencyPortal generates AP credentials and token; local HTTP portal has session 15min, idle 5min, action rate 3s. It uses GET for page/status and POST for actions.
5. V29 portal start can choose AP_STA when STA was previously active; main.cpp overlay inhibits normal STA reconnection while portal is active.
6. V28-master is four commits ahead of V29 merge base for files v27/apply_v27.py, v27/tests/test_contracts.py, v27_global_validation.yml and v27/release-trigger.txt. V28 contains additional privacy-preserving fallback tests.
7. V29 workflow uses queue:max and cancel-in-progress:false; other workflows are not thereby serialized.

## A–Z inspection checklist, evidence and action
A Architecture: defined; actual coordinator absent; define ownership before implementing.
B Board/pin map: missing verified board-revision schematic and GPIO ownership.
C Cryptography: uses mbedTLS; threat model and nonce/replay proof missing.
D Dedup: 256-entry seen ring; test replay after reboot and ring rollover.
E Emergency: 22-byte body; do not claim full chat.
F Flash: partition and OTA/rollback capacity unmeasured.
G GPS: no-fix/stale-fix UX, privacy and board UART mapping unverified.
H Hotspot: AP_STA behavior requires test; 2.4GHz only.
I Internet relay: preserve V28 authenticated fallback, no false delivery.
J Journaling: dual snapshots require fault injection at every write boundary.
K Keyboard: focus traversal and text input acceptance pending.
L LoRa: P1 V8 RF compatibility and legal profile verification pending.
M Memory: PSRAM allocation logic inspected; actual headroom and fragmentation unknown.
N Network: local portal has bounded request-line 192 and 300ms loop; slow-client/UI impact needs bench testing.
O Offline: core RF independent from SD/Internet by design; real regression missing.
P Portal: **critical review required** tokenOk() uses strstr(path,"t=") and strncmp(token length), which may accept a token prefix followed by extra bytes; token appears in URL query. Replace with exact parameter parsing and constant-time full token comparison; test malformed/duplicate params, no secret in logs. Avoid claiming secure as-is.
Q Queue: priority/fairness and eviction under adversarial load not behavior-tested.
R Radio: one MyMesh sender, need TX airtime budget and measured packet error rate.
S Security: portal access relies on local AP plus URL token; origin/CSRF and HTTP local trust need explicit threat model.
T Tropical UI: 14 screen sheets specified but wireframes, pixel bounds and focus map not delivered.
U USB/BLE: preserve existing behavior, do not claim unverified iPhone support.
V Versioning: overlay order and protocol v2 known; migrations and compatibility matrix pending.
W WiFi: STA/AP handoff test and 10-cycle reconnect required.
X Extensions: optional hardware OFF until electrical and driver compatibility verified.
Y Yield/watchdog: portal handleClient waits up to 300ms; instrument latency, verify no starvation.
Z Zero-regression: V27 privacy delta, V28 chat, P1 V8 and V29 tests must all pass.

## P0/P1 code-ready tasks, exact locations
P0-01 Port V28 privacy/function-preserving V27 overlay and tests, **without** release trigger; paths v27/apply_v27.py, v27/tests/test_contracts.py; compare exact blobs to v28-master; compile and run all contracts.
P0-02 Portal token parser/security review: v29/overlay/src/helpers/esp32/V29EmergencyPortal.cpp tokenOk(), handleClient(), sendPage(). Require exact token equality, no accidental substring match, bounded input, test GET side effects and POST authorization.
P0-03 Protocol threat model: V29EmergencyFabric.cpp verifyWire(), normalizedAuthData(), deriveDirectKey(), seenOrRemember(), snapshot recovery. Verify nonce reuse, signature scope, age tampering and reboot replay.
P0-04 CI baseline: .github/workflows/v29_validation.yml; record actual PlatformIO version from passing baseline, pin, run once, retain log and binary hashes; inspect all active workflow triggers before shared concurrency changes.
P0-05 Resource budget: actual binary/partition, heap largest/free, stack watermarks, PSRAM, boot modes and battery power measurements; hardware required.
P1-01 Complete 14 UI screen sheets, interaction graph and error states; UITask.cpp integration map.
P1-02 P1 Pro V8 RF matrix, two T-Deck test, Android/iPhone hotspot; record device revisions and radio parameters.
P1-03 Durable queue corruption and power-cut matrix; simulated faults plus physical tests.
P1-04 Signed/checksummed release, installer smoke and rollback instructions.

## Definition of Ready (DoR) for first coding PR
1. Scope + file list + rollback + test IDs recorded.
2. Known critical security problems have owner and exact remediation; security-sensitive changes require review before merging.
3. Baseline branch is backed up, upstream SHA pinned, no automatic release/publish.
4. Each task has reproducible unit/static tests; firmware changes require build.
5. Resource assumptions marked estimates until measured.
6. Review decisions explicitly documented by S1 embedded, S2 RF, S3 security, S4 UX, S5 QA.
7. No claim of PASS without attached log/test/artifact SHA.

## Current gate decisions
PC1 audit: PARTIAL — key sources checked, 44 workflow full inventory, board revision and measurements pending.
PC2 design: PARTIAL — deep atlas and blueprint present, 14 pixel wireframes and security review missing.
PC3 handoff: PARTIAL — P0/P1 task order now explicit, acceptance and evidence still missing.
**Firmware coding authorization: NOT YET.** First permissible next activity is scoped baseline reconciliation plus verified test harness; release remains blocked until hardware validation.

## Evidence template
ID | Requirement | Source SHA | Board revision | Procedure | Expected | Actual | Log SHA | Owner | Reviewer | PASS/FAIL/BLOCKED.
