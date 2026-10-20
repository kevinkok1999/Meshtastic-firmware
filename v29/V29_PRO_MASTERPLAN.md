# MeshOffGridNL V29 PRO — MASTERPLAN
Status: PRE-CODING PLAN, not implemented or hardware-verified. Date: 2026-10-20.
Target: ONE V29 PRO release. V28 native chat, RF-first semantics, P1 Pro V8 compatibility and existing V29 emergency fabric are mandatory baselines.

## Five specialist workstreams
S1 EMBEDDED/PERFORMANCE — ESP32-S3, display, touch, keyboard, trackball, SPI/pins, DRAM/PSRAM/flash, battery, storage. Deliver hardware inventory and measured budgets.
S2 RF/MESH/CONNECTIVITY — SX1262 EU868, legal airtime, P1 interoperability, WiFi 2.4 GHz, BLE/USB, message transports, link-quality testing. Deliver RF matrix and interface contracts.
S3 SECURITY/RESILIENCE — keys/identities, reviewed E2EE, relay/portal authentication, replay, recovery, safe update and emergency trust. Deliver threat model and security gates.
S4 PRODUCT/UX — tropical wallpaper, 320x240 screens, icons, navigation across touch/trackball/keyboard, browser portal, accessibility. Deliver screen map and testable flows.
S5 INDEPENDENT QA/RELEASE — regression, single-runner CI, soak/hardware testing, installer, hashes, evidence ledger. Deliver go/no-go review.
Project lead coordinates all five roles and shared GitHub documentation. Roles are planned responsibilities; no claim of five independent agents already working.

## Non-negotiable rules
- Exactly one V29 PRO release; no V30/V31.
- Do not introduce a second ordinary chat engine, duplicate bubbles, or a second unreviewed RF path.
- Local RF messaging works without cloud, SD, WiFi or phone.
- Preserve historical release artifacts and reconcile divergent v28-master/v29-emergency-fabric branches before code changes.
- No false delivery acknowledgments or 112/NL-Alert impersonation.
- EU868 compliance; no jamming, arbitrary PA boosting, SDR or 5GHz claims on stock T-Deck.
- Unsupported extensions remain disabled. Bound queues, caches, retries and logs.
- Never claim PASS without test evidence; UNKNOWN is acceptable.

## Phase 1 — Audit (no firmware edits)
A1 Pin upstream WadaMesh and all overlay SHAs, compare V28/V29 divergent commits, write reconciliation plan.
A2 Inventory existing implementation, radio profile, platformio env, partition table, pins, drivers, memory and CI workflows.
A3 Classify each capability PRESENT/PARTIAL/ABSENT/UNKNOWN from source evidence.
A4 Reproduce baseline build and measure firmware size, free DRAM/PSRAM, boot, task stack, energy and RF performance.
A5 Verify V28 contracts, P1 Pro V8, cryptographic dependencies, OTA feasibility, installer and runner conflicts.
GATE G1: traceable baseline, risk register, compatibility map and reproducible build procedure.

## Phase 2 — Detailed pre-coding contracts
Each module must specify requirement ID, owner, current code map, API/events, state machine, persistent schema, RAM/PSRAM/flash/power budgets, timeout/error/recovery, privacy/security, unit tests, hardware tests and acceptance threshold.

UX01 Tropical UI/wallpaper/icons: 320x240 readable, fallback asset, touch/trackball/keyboard parity, no dead ends.
UX02 Home/emergency/settings/browser: meaningful CTA, back, empty/error states, accessibility and clear delivery status.
MSG01 DM/channels/contacts/invites: preserve V28 native core, 8-character invite semantics, one logical history.
MSG02 Message lifecycle: unique IDs, encrypted payload, bounded durable queues, priority fairness, TTL, dedup, replay and truthful ACK distinctions.
NET01 WiFi: one reconnect owner; association/IP/DNS/TLS/relay states separate; Android/iPhone hotspot matrix; no default unsafe open-network joins.
NET02 Global relay/local portal: optional internet, TLS/authentication, rate limits, POST-only emergency actions, no portal access to keys.
NET03 BLE/USB: capability-gated pairing, bounded buffers, reconnection and failure isolation.
RF01 SX1262 profiles: lawful EU868, RF-first, P1 V8 compatibility, airtime accounting and controlled A/B.
RF02 Diagnostics: RSSI/SNR/CAD where supported, packet history, confidence and mesh-node view; no false SDR/VSWR claims.
RF03 Coverage/antenna: opt-in GPS logs, range/PDR/latency/battery A/B; do not promise gain without evidence.
LOC01 GPS/waypoints/sharing: age/accuracy indication, privacy consent, optional SD history.
SEC01 Identity/E2EE: reviewed crypto, key lifecycle, trust, replay protection, no invented algorithms.
SEC02 Updates/portal: signed updates and rollback only after partition verification, CSRF/session controls, separate reset types.
SYS01 Storage/power: atomic journal/config, bounded queues, SD optional, watchdog, brownout recovery, realistic external-power 192h target.
SYS02 Device health/automation/docs: accurate diagnostics, redacted logs, bounded rules, offline help.
EXT01 NFC/IR/sensors: verified external hardware, pin conflicts, drivers and power before enabling.
REL01 CI/installer: one runner serialized across workflows, caches, checksums, reproducibility and rollback documentation.
GATE G2: S1–S5 sign off interfaces, threat model, UX, budgets, test matrix and compatibility.

## Phase 3 — Build and verify
Implement in dependency order: core contracts -> storage/security -> RF/connectivity -> messaging -> UI -> optional extensions -> installer. Use scoped PRs and feature flags. Cheap static/unit checks precede serialized PlatformIO CI. Test on real T-Deck Plus devices plus P1 Pro V8: LoRa delivery, WiFi hotspots, BLE, GPS, SD removal, power loss, touch/trackball/keyboard, RF A/B, memory pressure and soak. Preserve existing tests. Record evidence and defects.
GATE G3: release only when all critical tests pass and artifacts have verified hashes; otherwise label DEV/RC.

## Evidence ledger
For each requirement record: ID | owner | code path | test ID | baseline | result | evidence URL | status (UNKNOWN/DESIGNED/IMPLEMENTED/VERIFIED/BLOCKED).
Scoring: 10/10 only with complete verified acceptance; no speculative scores.

## Immediate next actions
1. Complete Phase 1 branch reconciliation and source inventory.
2. Create API and UX contracts for Phase 2 with resource budgets.
3. Review all GitHub Actions concurrency before triggering any build.
4. Obtain actual T-Deck wallpaper asset and hardware measurements.
5. Hold S1–S5 review before Phase 3 code changes.
