# V29 PRO — Issue Resolution Log 02
Date: 2026-10-20. Scope: non-firmware changes only.

## Completed fixes
FIX-01: .github/workflows/v29_validation.yml push paths narrowed from v29/** to v29/apply_v29.py, v29/overlay/**, v29/tests/** and workflow itself. Documentation-only edits under v29/ no longer trigger firmware builds. NOTE: changing the workflow itself DOES trigger validation.
FIX-02: Changed cancel-in-progress to false to avoid terminating a running V29 validation when another change is pushed. NOTE: default concurrency may replace an existing pending run; this is NOT a full FIFO queue.
## Evidence-based findings
- GitHub compare v28-master...v29-emergency-fabric: diverged, 85 ahead and 4 behind at audit time; merge base 4815b06245279b24861732c2cdec59c86e3f11e5. Do NOT merge blindly.
- Repository contains 44 workflows. Reviewed V26/V27/V28 validation workflows use separate groups and older branch-scoped triggers. V29 group does not serialize across unrelated workflows. Single-runner-wide control remains unresolved.
- v29_validation.yml pins upstream WadaMesh SHA but installs PlatformIO dynamically. Toolchain reproducibility remains unresolved.
- Static contract tests are not sufficient for runtime security/reliability.
## Open blockers
B1 compare four behind commits and reconcile only necessary V28 fixes.
B2 inspect remaining workflow triggers and choose shared build group, ensuring no unwanted cancellation and preserving historical release jobs.
B3 establish exact firmware resource baseline and partition/rollback proof.
B4 produce code-level interface contracts, UI flows and security threat model.
B5 hardware acceptance evidence: P1 compatibility, RF A/B, WiFi hotspot, power and storage faults.
## Quality gates
PC1 PARTIAL, PC2 PARTIAL, PC3 BLOCKED. No release readiness claim. Any workflow triggered by this commit should be inspected before declaring PASS.
