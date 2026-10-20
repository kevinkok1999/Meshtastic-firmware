# MeshOffGridNL V29 PRO — Audit sprint 01 (2026-10-20)
Status: SOURCE INSPECTION ONLY. No builds or hardware tests run. Companion to v29/V29_PRO_MASTERPLAN.md.

## Evidence inspected
- v29/MASTER_SPEC.md on v29-emergency-fabric: Dutch emergency fabric, 192h external-power engineering target, bounded queues, offline core, local portal, P1 compatibility.
- v29/apply_v29.py: applies V29 overlay to V28-patched WadaMesh, modifies platformio.ini, MyMesh and UI; includes raw emergency send hook.
- v29/tests/test_contracts.py: checks required symbols and UI markers, forbidden Internet dependencies in offline core; these are largely static presence checks, not proof of runtime correctness.
- .github/workflows/v29_validation.yml: pinned upstream beta_83 SHA 6fe6b9f06332708b6ed0cc78958017d0dcc9e35d, overlay sequence V11,V12,V13,V15,V16,V17,V18,V19,V26,V27,V28,V29, contract scripts and T-Deck PlatformIO build; size guard < 0x3E0000.
- LILYGO official T-Deck Plus docs: ESP32-S3 16MB flash, 8MB PSRAM, ST7789 320x240, GT911 touch, keyboard, trackball, SX1262, GPS, 2.4GHz WiFi/BLE, TF. Shared SPI CS: LoRa 9, TFT 12, SD 39. GPIO10 HIGH on battery power. GPS occupies Grove pins.

## Initial findings (not full code audit)
F01 HIGH: The current V29 spec is emergency-focused while planned V29 PRO scope includes broader UI, RF intelligence and hardware extensions. Need traceable reconciliation, not silent replacement.
F02 HIGH: v29_validation.yml concurrency group is local to that workflow. It does not guarantee serialization with other repository workflows on a single runner.
F03 HIGH: v29_validation.yml fetches PlatformIO dynamically without a pinned version and uses pip upgrades; reproducibility and caching need review.
F04 MEDIUM: Contract tests check code markers; additional behavioral, security and hardware tests required.
F05 HIGH: Source pin conflicts possible for external modules; T-Deck Plus Grove pins are already used by GPS. External NFC/IR must remain design-only until validated.
F06 HIGH: No measured DRAM/PSRAM, power, RF PDR, OTA partition or UI latency baseline was obtained during this inspection. All remain UNKNOWN.
F07 MEDIUM: Existing emergency fabric exposes a separate raw emergency envelope sender. Audit its interactions with the native V28 chat and airtime before integrating further messaging.
F08 HIGH: 192h is an engineering target with adequate external power, not an observed battery-life result.
F09 HIGH: The source audit does not yet establish that the current branch is compatible with all v28-master commits; branch divergence reconciliation remains mandatory.

## Phase 2 design deliverables — assigned
S1 Embedded: hardware pin/peripheral ownership map; task/heap/PSRAM budget; SPI arbiter requirements; UI frame budget.
S2 RF/Connectivity: legal EU868 profile review; radio airtime scheduler; transport state machines; P1 V8 RF contract; controlled A/B plan.
S3 Security: protocol threat model; key lifecycle; portal/session model; replay/nonce tests; power-loss journal integrity.
S4 UX: 320x240 screen hierarchy, Tropical wallpaper crop/contrast, touch/keyboard/trackball interaction maps and all CTA paths.
S5 QA: acceptance case IDs for every feature, baseline regression, one-runner cross-workflow serialization policy, installer/rollback evidence.

## Next audit actions (before firmware modifications)
1. Enumerate all .github/workflows files and identify competing triggers.
2. Inspect platformio.ini, partition layout, pin usage and full V29EmergencyFabric/Portal implementation.
3. Compare branch heads v28-master vs v29-emergency-fabric and reconcile.
4. Create interface contracts and acceptance test matrix based on actual source APIs.
5. Only then authorize a baseline build; avoid burning runner time on docs commits.

## Gate status
G1 baseline: BLOCKED (incomplete source inspection, branch reconciliation and measurements).
G2 design: IN PROGRESS (masterplan written, module contracts not finalized).
G3 implementation: NOT STARTED.
