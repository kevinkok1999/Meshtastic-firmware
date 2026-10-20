# V29 PRO — THREE-STAGE PRE-CODING PROGRAM
Date: 2026-10-20. Status: DESIGN PROGRAM — not a claim that gates have passed.
This document subdivides PRE-CODING into THREE STAGES. Coding begins only AFTER PC3 approval. This is distinct from the overall audit/design/implementation roadmap.

## Team and decision rights
S1 Embedded & Resources: feasibility, drivers, SPI arbitration, partitions, heap/PSRAM, power.
S2 RF & Connectivity: EU868, SX1262, P1 Pro V8, mesh, WiFi/BLE, relay transport.
S3 Security & Resilience: identity, crypto, portal, replay, journal and recovery.
S4 UX & Product: Tropical 320x240 interface, navigation, accessibility, browser.
S5 Independent QA & Release: evidence, test traceability, CI, installer, quality gates.
Coordinator owns conflict resolution and requirements traceability. These are defined roles; specialist review is PENDING until evidenced.

## PC1 — RECONCILE AND AUDIT (entry: masterplan)
Deliverables:
- Branch ancestry/commit difference ledger; exact V28 and V29 SHAs, upstream and overlays.
- Source inventory and truth table PRESENT/PARTIAL/ABSENT/UNKNOWN for all modules.
- Pin/peripheral ownership matrix, build/partition/OTA constraints, dependency versions.
- CI workflow inventory, cross-workflow concurrency plan for ONE runner, zero redundant builds.
- Risk register severity/owner/mitigation and baseline measurement plan.
Evidence as of 2026-10-20: GitHub compare v28-master...v29-emergency-fabric reports DIVERGED, 85 ahead / 4 behind, merge base 4815b06245279b24861732c2cdec59c86e3f11e5. No merge approved. Existing v29_validation.yml group is workflow-specific, so cross-workflow serialization is unproven.
Gate PC1: no unexplained conflicting code paths, baseline and compatibility matrix documented. Actual hardware measurements may remain BLOCKED with named test procedure; cannot be treated as PASS.

## PC2 — COMPLETE DESIGN AND CONTRACTS (entry: PC1 review)
Architecture decisions:
- Single native V28 chat core and logical message identity; V29 emergency bundles capability-gated and do not become a competing chat engine.
- RF core works offline. Internet relay and phone portal optional; independent connection state machines and bounded retries.
- All module APIs versioned and error-aware; no UI thread blocking. Each module must document owner, events, state transitions, persistent schema, queue bounds, memory/power budget, security, fallback, test IDs.
- DRAM is safety-critical; 80% PSRAM/cache target is never an allocation requirement. Budget must be measured against baseline and worst-case fragmentation.
- SX1262 measurements are packet/channel indicators, not wideband SDR, SWR or guaranteed noise cancellation.
- Hardware expansions (NFC/IR/sensors) OFF unless actual wiring, voltage, pin conflicts, drivers and tests are approved.
- GPS sharing opt-in, emergency portal truthfully labels local mesh requests as NOT emergency services.
Required design packs:
A UX: Tropical wallpaper asset specification, navigation sitemap, all touch/keyboard/trackball states, accessibility and browser.
B Messaging: DM/channels, invite flow, envelope and ACK schema, queue/dedup/TTL/replay, store/carry/forward.
C Transport: RF-first, WiFi 2.4 GHz hotspot matrix, BLE, internet relay, local AP portal, USB capability detection.
D RF: EU868 legal profile, P1 V8 compatibility, RSSI/SNR metrics, controlled antenna A/B, coverage atlas confidence.
E Security: identity/key lifecycle, crypto review, threat model, signed update/rollback feasibility.
F Device: power modes, safe mode, storage journaling, SD removal, diagnostics and extensions.
G Delivery: installer, hashes, versioning, CI runner policy, offline documentation.
Gate PC2: S1–S5 written reviews of each pack, documented assumptions and acceptance thresholds, no unresolved CRITICAL design risks.

## PC3 — CODE-READY HANDOFF (entry: PC2 approval)
Deliverables:
1. Dependency-ordered implementation tickets with exact target paths, input/output contracts, tests, rollback strategy and acceptance criteria.
2. Contract-test fixtures and behavioral test plans; negative cases for replay, malformed packets, brownout, no SD, relay down, no GPS, WiFi fail and memory pressure.
3. Hardware validation scripts: two T-Deck Plus nodes + P1 Pro V8, Android/iPhone hotspot, touchscreen/keyboard/trackball, RF A/B, SD removal and power measurement.
4. CI execution plan: cheap static checks first, one serialized PlatformIO job, deterministic tool versions and dependency cache, firmware size and artifact hashes.
5. Release evidence ledger template: requirement | owner | code | test | expected | actual | artifact | status. A PASS requires reproducible evidence.
6. Final implementation order: core resource/safety contracts → identity/storage → RF/transport → messaging → UX → optional diagnostics/extensions → installer.
Gate PC3: each ticket independently implementable without guessing, dependencies resolved, test fixtures specified, S1–S5 sign-off recorded. ONLY THEN start coding.

## Quality bar
- Critical: zero unmitigated risks before coding; zero critical failures before release.
- Each requirement: observable behavior + explicit negative tests + bounded resource use.
- No 9/10 or 10/10 based on promises. 10 requires complete verified acceptance.
- 192-hour resilience is an external-power engineering target, not demonstrated battery runtime.
- No firmware code, workflows or builds changed as part of this planning document.

## Immediate blockers
B1 branch reconciliation: DIVERGED 85/4; inspect four missing base commits before merge.
B2 enumerate all workflow triggers and coordinate cross-workflow single-runner behavior.
B3 real firmware size, DRAM/PSRAM, battery and RF baselines unknown.
B4 complete protocol/API definitions and UI screen-by-screen designs not yet frozen.
B5 real hardware evidence unavailable in this stage.
