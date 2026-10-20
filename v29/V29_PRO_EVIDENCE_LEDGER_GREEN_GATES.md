# V29 PRO — Evidence Ledger and Green-Gate Matrix (2026-10-20)
**Purpose:** make pre-coding genuinely executable. GREEN means verified evidence, never just a planned feature. All items are for ONE V29 PRO release.

## Verified CI evidence (new)
GitHub Actions run 39437122981, head f9aa5e4ebfc9cb879a163cdd8aa267c0035dbb8a, conclusion SUCCESS. Job 'V28 baseline + V29 offline emergency core' SUCCESS. Steps confirmed SUCCESS: pinned upstream checkout, apply V11→V29, V29/V28/V26 contract scripts and V29 resilience model, pinned P1 V8 JSON contract, PlatformIO installation, LilyGo_TDeck_companion_radio_touch compilation, firmware image-size guard. This **is real CI compile evidence**, but not hardware/field proof. URL: https://github.com/kevinkok1999/Meshtastic-firmware/actions/runs/39437122981.
Another run 39436464168 was reported in_progress at inspection, so do not assert all runs completed.

## RAG readiness register
G01 Build baseline: GREEN for SHA f9aa5e4, compile+size only. Revalidate after every source change.
G02 Source/overlay integrity: GREEN for same run's patch/test steps, not runtime correctness.
G03 P1 Pro V8 profile static check: GREEN for JSON contract, hardware interop AMBER.
G04 V28/V29 divergence: RED, four missing V28 changes, especially privacy fallback protections; review and scoped port needed.
G05 Portal security: RED, V29EmergencyPortal.cpp tokenOk() uses substring + prefix comparison; query-token exposure and local HTTP threat model unresolved.
G06 Crypto correctness: RED until nonce/replay/age tamper and recipient-binding negative tests.
G07 Hardware power, partition, heap: AMBER; build size guard succeeded, actual flash partition and PSRAM/stack/power data absent.
G08 Radio legal and P1 interoperability: AMBER; static RF profile check is not on-air compliance or real communication.
G09 WiFi AP/STA, Android/iPhone: AMBER; source interlock exists, real tests absent.
G10 Storage crash recovery: AMBER; two SPIFFS snapshots exist, fault injection missing.
G11 14 UI wireframes and keyboard/touch: AMBER; screen index documented, interaction evidence missing.
G12 Release packaging/installer: AMBER; build artifact/size guard present, full release validation pending.
G13 CI one-runner cross-workflow: AMBER; V29 queue works, all other active build workflows not harmonized.
G14 PlatformIO dependency reproducibility: AMBER; version is installed dynamically, not pinned.

## Definition of GREEN
Every gate record must contain: commit SHA, source files, exact test command, expected result, actual result, CI run URL or physical device log, date, owner, reviewer. Hardware-only requirements remain AMBER until measured. If a red critical security item exists, firmware release is NO-GO.

## Next coding handoff: prioritized, independently verifiable
T01 Baseline sync: diff v27/apply_v27.py and v27/tests/test_contracts.py against v28-master, port only privacy-preserving safety changes; run V27/V28/V29 contract suites and full build. Do not touch historical release-trigger.
T02 Portal: fix exact token parameter parsing, secret comparison and token-in-URL handling; introduce negative parser tests and POST/GET abuse tests; run full build. Review AP password/session trust and no-key-exposure.
T03 Cryptographic audit: record byte-level wire layout; test invalid tag, signature, recipient, replay, mutable relay-age manipulation, nonce after reboot; only implement changes justified by tests.
T04 Runner: classify 44 workflows by active triggers; move ONLY active firmware builds to a shared concurrency group, retain queue:max; no auto-publishing.
T05 Reproducibility: read PlatformIO version from known-good run logs, pin verified version, record dependency versions, clean-build twice and compare checksums where deterministic.
T06 UI: write 14 actual pixel wireframes and input focus/back/error transitions, then approve S4.
T07 Hardware: run two T-Deck devices + P1 Pro V8, low-power tests, hotspot compatibility, SPIFFS brownout; record model/revision, logs and measured thresholds.
T08 Final: regression matrix, OTA/recovery path, SHA256 release manifest, installer smoke and S1–S5 reviews.

## Coding decision
**ALLOW scoped engineering correction work T01–T03 with a regression-first approach.** Full feature coding and release publication remain gated. No imaginary green checks. Progress should be measured by closed test-backed issues rather than number of planning documents.
