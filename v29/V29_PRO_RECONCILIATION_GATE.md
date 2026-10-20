# V29 PRO — Reconciliation and Coding Gate (2026-10-20)
Status: EVIDENCE-BASED AUDIT; coding NOT yet authorized. Companion to V29_PRO_CODE_BLUEPRINT.md.

## Resolved with actual repository evidence
- Compared v29-emergency-fabric → v28-master: **four commits ahead on V28**, touching ONLY v27/apply_v27.py, v27/tests/test_contracts.py, .github/workflows/v27_global_validation.yml and v27/release-trigger.txt. This is not an arbitrary conflict across the firmware.
- V28 adds a V27_PRIVACY_FUNCTION_PRESERVING build flag, safeguards for privacy/global-first fallback, and tests requiring authenticated global delivery to succeed before bypassing RF, rejecting false success after RF failure, forbidding Wi-Fi/RF teardown by bridge and forbidding TLS setInsecure.
- V28's v27 workflow adds a line, and release-trigger changes; preserve historical release metadata rather than cherry-picking release triggers blindly.
- V29 CI now uses queue: max with cancel-in-progress: false, so pending V29 validation runs are preserved. It is NOT a cross-workflow runner lock. GitHub documentation confirms queue: max supports up to 100 waiting runs.

## Priority issue R1: Privacy-preserving fallback (CRITICAL)
Decision: the newer V28 protections are required before V29 coding. Integrate V27_PRIVACY_FUNCTION_PRESERVING and corresponding regression assertions by a scoped, reviewed port; verify their effect on actual patched upstream code. DO NOT merge all V28 commits blindly, and DO NOT modify V27 release triggers or publish older releases.
Required checks:
1. V27 apply script inserts flag exactly once and remains idempotent in its expected apply order.
2. V27 contracts validate encrypted publish success before global-first short circuit, otherwise native RF remains available.
3. No global queue false-success on RF failure.
4. No bridge WiFi teardown or TLS verification bypass.
5. V28 and V29 contract suites pass together.
Owner S2+S3; reviewer S5. Current status: OPEN — precise difference understood, not yet ported or tested.

## Priority issue R2: Single-runner serialization (HIGH)
V29 workflow-only group protects V29 runs; other workflows may have different groups. Need enumerate all 44 workflows and choose shared concurrency group ONLY for active firmware-build workflows. Prefer queue: max, cancel-in-progress: false; test validation syntax and behavior. Historical workflows must not be rewritten indiscriminately.
Owner S5. Current status: PARTIAL — V29 queue fixed; global coordination open.

## Priority issue R3: PlatformIO reproducibility (HIGH)
Upstream commit is pinned, but Python pip upgrades and PlatformIO are not pinned. First inspect actual installed PlatformIO/toolchain versions from successful baseline, then pin known-good versions, cache packages and record lock manifest. Do not guess a version.
Owner S1+S5. Status OPEN.

## Priority issue R4: Hardware budgets (CRITICAL)
Read partition table and actual binary size; obtain DRAM/PSRAM largest free block, stack watermarks, power profiles, SPI ownership and P1 RF interoperability. No evidence collected yet; targets remain provisional.
Owner S1+S2. Status OPEN.

## Coding entry gate
Authorize first firmware implementation PR only when:
- R1 reconciled with passing V27/V28/V29 contracts;
- R2 documented and active build workflows serialized;
- baseline builds reproducibly and firmware fits verified partition;
- S3 threat model reviewed, no critical protocol vulnerabilities unmitigated;
- S4 exact screen-by-screen wireframes and interaction states;
- S5 requirement-to-test matrix and evidence ledger completed.
Until then, pre-coding work may continue, but calling the system CODE-READY would be misleading.

## Source refs
Compare: v29-emergency-fabric...v28-master, merge base 4815b06245279b24861732c2cdec59c86e3f11e5.
Relevant V28 files: v27/apply_v27.py, v27/tests/test_contracts.py, .github/workflows/v27_global_validation.yml, v27/release-trigger.txt.
