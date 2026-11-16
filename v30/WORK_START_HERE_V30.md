# V30 PRO — engineering handoff

Status: PLANNING ONLY. No V30 firmware, map pack, CI or production release exists from this branch initialization.

1. Review [Master spec](MASTER_SPEC.md).
2. Execute [Foundation](../docs/superpowers/plans/2026-11-16-v30-foundation.md), then [RF/power](../docs/superpowers/plans/2026-11-16-v30-rf-power.md), then [MeshMap Nano](../docs/superpowers/plans/2026-11-16-v30-meshmap-nano.md). Map data sizing can run independently; integration depends on resource arbitration.
3. Require red-green executable tests, pinned dependencies, full applicable regression, exact artifact hashes and physical evidence before marking any gate complete.
4. Development only: do not trigger existing V29 publication workflows, modify any historic tags, or promote V30 to production. Keep 112 distinct and do not make commercial claims from CI alone.

Baseline: 18b9f4d4c00c372b5d458c4fbb8c9779f80c6b91.
