# MESHOffGridNL V29 PRO — WORK START HERE
**Primary instruction for ChatGPT Work:** When the user says "Ga door met V29", execute this document as the canonical, durable, repo-resident handoff. Do not merely write plans. Perform code changes, test, inspect CI, resolve failures, and deploy only when release gates pass.

## User intent and fixed constraints
- Exactly ONE comprehensive release **V29 PRO**, not V30/V31 or feature postponement.
- Firmware repository: `kevinkok1999/Meshtastic-firmware`; development branch `v29-emergency-fabric`.
- Website repository: `kevinkok1999/Meshoffgridnl`; production branch `main`; Vercel project `meshoffgridnl`; installer `/nl/installeren/`, `/en/install/`, `/de/installieren/`.
- Devices: LILYGO T-Deck Plus ESP32-S3 with SX1262 EU868 and SenseCAP P1 Pro V8. Existing V28/P1 behavior must not regress.
- Single GitHub runner budget: minimize redundant builds; serialize relevant CI jobs. Never assume cross-workflow concurrency from one workflow's group.
- Work with five review lenses S1 Embedded, S2 RF, S3 Security, S4 UX, S5 QA. They are review responsibilities, not fictitious humans/agents.
- Preserve offline RF fallback, existing messages, user data and previous installer releases. No factory wipe or OTA flash on users' devices without explicit authorization.
- Work autonomously on source and tests within scope. Never fabricate test results, physical device access, completed reviews or release approval.

## Three execution phases — implement and prove, not just document
### PHASE 1 — Baseline reconciliation, security and deterministic CI
1. Fetch fresh GitHub branch HEAD and inspect current run status; avoid stale assumptions. Last known V29 run for commit `d7053149` was **in_progress** on 2026-10-20: https://github.com/kevinkok1999/Meshtastic-firmware/actions/runs/39438983889 . Prior run for portal token fix `2a96ee36` succeeded: https://github.com/kevinkok1999/Meshtastic-firmware/actions/runs/39438254398 .
2. Reconcile V28 baseline: `v27/apply_v27.py` has `V27_PRIVACY_FUNCTION_PRESERVING=1` added at `344007a`; `v27/tests/test_contracts.py` copied V28 privacy invariants at `4728e18`. Compare against v28-master and port any remaining *relevant* safeguards; **do not** blindly merge release-trigger changes.
3. Portal `v29/overlay/src/helpers/esp32/V29EmergencyPortal.cpp` tokenOk() fixed to reject partial/duplicate tokens at `2a96ee36`. V29 contract markers added at `d7053149`. Evaluate token in GET URL, local HTTP, token entropy, CSRF/origin, input bounds, response headers; add behavioral tests (not just source markers). Do not claim this portal is fully secure merely because prefix matching was fixed.
4. Audit V29EmergencyFabric.cpp crypto nonce lifecycle, signature/recipient binding, authenticated mutable age, replay after restart, direct-key derivation, 184-byte wire and 22-byte body constraints, queue eviction, snapshot corruption/recovery. Implement failing negative tests first; fix concrete failures.
5. Pin PlatformIO/dependencies to known-good versions, inspect `.github/workflows/v29_validation.yml`, CI runner overlap and all active firmware workflows. Preserve known-good WadaMesh SHA `6fe6b9f06332708b6ed0cc78958017d0dcc9e35d`. Build once per coherent patch batch; require green CI logs and size guard.
**Phase 1 exit:** new source SHA with passing full build, static and behavioral tests, no known critical security issues; publish evidence ledger. If no actual hardware access, mark hardware-dependent gates BLOCKED rather than green.

### PHASE 2 — Complete V29 firmware, UX and interoperability
1. Read full specifications `v29/V29_PRO_MASTERPLAN.md`, `v29/V29_PRO_CODE_BLUEPRINT.md`, `v29/V29_PRO_DEEP_ZOOM_ATLAS.md`, `v29/V29_PRO_ENGINEERING_CONTRACTS_DRAFT.md`, `v29/V29_PRO_A_TO_Z_CODING_ENTRY_AUDIT.md`, `v29/V29_PRO_EVIDENCE_LEDGER_GREEN_GATES.md`.
2. Inspect real firmware source and patch pipeline `v29/apply_v29.py`, `v29/overlay/src/helpers/esp32/V29EmergencyFabric.{h,cpp}`, `V29EmergencyPortal.{h,cpp}`, `v29/tests/test_contracts.py`, `v29/tests/test_resilience_model.py`; don't assume blueprint features are implemented.
3. Implement missing requirements through scoped commits and regression tests: 14 Tropical 320x240 screen interactions; emergency contact check-in/help; GPS no-fix/privacy; RF-first and authenticated internet fallback; reliable local portal; power states and PSRAM/flash budgeting; durable storage and restore. No unnecessary feature creep or new version.
4. Verify P1 Pro V8 compatibility without changing radio waveform accidentally; RF parameters in current static contract: 869.618 MHz, BW62.5kHz, SF8, CR5, configured TX22dBm (not proof of legal radiated EIRP). Require actual antenna/regulatory validation before release.
5. Produce board-specific bench checklist for two T-Deck Plus units and one P1 Pro: flash, cold boot, offline send/receive, 8-day TTL logic, queue rollover, battery modes, Android hotspot 2.4GHz, iPhone compatibility as supported, 10 reconnect cycles, portal timeout, SPIFFS power-cut, heap/stack metrics. Execute only if devices accessible; otherwise supply precise instructions and leave BLOCKED.
**Phase 2 exit:** implementation traceability matrix requirement→file→test→result, no hidden TODOs, complete CI pass, physical checks evidenced or explicitly blocked.

### PHASE 3 — Safe production website V29 installer
1. Inspect Vercel `meshoffgridnl` production deployment and website source. On 2026-10-20 production READY at commit `a85e3e7`; V29 visible on NL/EN/DE installer pages as **in development**, deliberately not selectable. Existing `installer-router.js` release keys stop at v28.
2. Read `installer-router.js`, `installer.js`, third-party installer, NL/EN/DE pages, existing firmware release manifests and V28 release pipeline. Preserve V1–V28 and P1 installers.
3. Only after validated release artifact: publish versioned V29 firmware manifest with pinned Git commit, byte size, SHA-256 and flash layout; make V29 selectable, ensure installer uses same verified artifact and does not silently fall back to V28 or an unpinned URL. Verify device model, browser support, preflash confirmation, data preservation/wipe warning, download checksum and recovery.
4. Add NL/EN/DE coherent release wording, deploy through existing Vercel Git integration, verify READY deployment is exact website commit, inspect production page and installer end-to-end. Do not use a fabricated artifact or enable installation before gates.
**Phase 3 exit:** website production READY on expected commit, V29 actually downloadable and installable with correct verified artifact, regression and safety gates pass. If hardware proof is absent, leave V29 as 'in ontwikkeling' and report blockers; do not fake a finished release.

## Work loop (repeat within Work execution)
A. Fetch branch/CI state and inspect changed files.
B. Pick highest-impact unblocked task from `v29/V29_PRO_EVIDENCE_LEDGER_GREEN_GATES.md`; create small reproducible failing test.
C. Implement exact fix with source commit.
D. Run targeted tests, then batched full CI; read jobs and logs, not just queued status.
E. Update evidence ledger with commit, test ID, CI URL, PASS/FAIL/BLOCKED.
F. Advance phase only after exit criteria. Publish concise checkpoint with real commits and remaining blockers.
G. Never claim unattended background work in ordinary chat; Work may operate through its own authorized execution.

## Current known state / evidence
- Firmware branch latest source commits include `344007a` V27 privacy flag, `2a96ee36` portal exact-token fix, `4728e18` V27 regression invariants, `d7053149` V29 portal static regressions.
- Last **confirmed successful** firmware build at `2a96ee36`; subsequent `d7053149` run not yet confirmed complete when this file was authored.
- Website source commits: `42233c0` NL, `cea11c3` EN, `a85e3e7` DE. Vercel deployment READY for `a85e3e7`.
- Hardware tests have **not** been evidenced; no release artifact certified.
- Critical follow-up: portal exact-token source check is NOT equivalent to independent parser tests; URL query tokens remain exposed in local HTTP/browser history.

## Finish criteria and final report
Return to user with: implemented features by requirement, source and website commit SHAs, passing CI job URLs, firmware artifact manifest SHA256 and size, production deployment ID, validated install flow, known limitations, device test evidence. If any release-blocking gate remains unverified, explicitly state **NOT RELEASED** and explain exact action needed; keep public installer safe.

## One-line trigger
**Ga door met V29. Lees eerst v29/WORK_START_HERE_V29_PRO.md en voer fase 1, 2 en 3 uit. Codeer en test daadwerkelijk; publiceer pas na bewezen release-gates.**
