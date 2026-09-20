# V7 Implementation Plan — 3 Phases

## Phase 1 — Import, preserve, instrument
Gate: V7 source builds and behaves like Saitama 1.3.0 before hybrid routing is enabled.

1. Import/fork Saitama v1.3.0 source at exact commit 16598b4e6a7eabd6195d06d2a3abc1b3b448f368 into an isolated V7 source location/branch. Preserve GPL notices.
2. Keep MeshCore pinned at d92964352441e53b93e8667b802e04f6e072b39e.
3. Establish V7 version naming without overwriting upstream historical tags.
4. Reproduce the t-deck build: 16MB, DIO, app + merged artifacts.
5. Add native/unit-test scaffolding that does not need the hardware runner where possible.
6. Add route/stat instrumentation around the existing MeshService boundary.
7. Verify LORA_ONLY parity: boot, UI, channels, DM, adverts, BLE, GPS/map startup, config migration, LoRa send/receive.

Do not implement website publication in this phase.

## Phase 2 — ESP-NOW LR + hybrid coordinator
Gate: two physical T-Deck Plus units exchange V7 traffic over ESP-NOW LR, and AUTO fallback to LoRa is deterministic.

Workstream A — Radio/ESP-NOW:
- Wi-Fi lifecycle manager replacing unconditional WiFi.mode(WIFI_OFF) only when required.
- station mode, fixed/managed channel, WIFI_PROTOCOL_LR.
- esp_now_init callbacks, peers, bounded queues.
- capability hello/probe, direct peer table, expiry.
- fragment/reassembly envelope, ACK frames and bounded retry.
- transport metrics.
- robust init/deinit and failure recovery.

Workstream B — Mesh/identity/security:
- map ESP-NOW MAC/capability to MeshCore contact identity.
- determine the clean encrypted payload serialization/injection seam in the pinned MeshCore/Saitama stack.
- keep normal LoRa packet path untouched for non-V7 peers.
- enforce no plaintext DM shortcut.
- stable message IDs and cross-transport dedup.

Workstream C — Policy/UI/config:
- HybridTransportManager with AUTO/LORA_ONLY/ESPNOW_LR_ONLY/REDUNDANT.
- append-only config migration.
- route/capability diagnostics.
- clear but minimal UI; existing messaging workflow stays the same.

Workstream D — Tests/CI:
- mocks for transport policy and deterministic timers.
- serialization/fuzz-like malformed frame tests.
- two-device hardware script/checklist.
- BLE/Wi-Fi coexistence matrix.
- memory/queue overflow checks.

## Phase 3 — Harden, release, installer preview
Gate: release candidate satisfies TEST_MATRIX and RELEASE_CHECKLIST.

1. Build exactly one T-Deck Plus target in CI; no matrix fan-out.
2. Generate app-only and merged binary.
3. Verify ESP32-S3 image structure, 16MB/DIO metadata, bootloader/partition offsets and artifact hashes.
4. Hardware smoke test on two T-Deck Plus units.
5. Backward-compatibility test against an unmodified Saitama/MeshCore node over LoRa.
6. Record SHA-256 + byte size in release metadata.
7. Tag immutable V7 release only after validation.
8. In MeshOffGridNL website repo, create a V7 installer change on a non-production branch:
   - add v7 release selector;
   - proxy/pin exact V7 release asset;
   - validate byte size + SHA-256 before erase/flash;
   - keep v1-v6 untouched;
   - add NL/EN/DE card text.
9. Let Vercel create/serve a preview and test installer page/routes there first.
10. Promote/merge to production only after preview QA and a real T-Deck flash succeeds.

## Stop conditions
Coding must stop and surface evidence instead of guessing if:
- the imported source is not exactly the locked upstream;
- the MeshCore submodule differs unexpectedly;
- a proposed ESP-NOW integration requires weakening encryption;
- the app no longer boots in LORA_ONLY;
- BLE coexistence produces reproducible instability;
- merged image is not a valid 16MB/DIO T-Deck Plus image;
- the website release metadata cannot be pinned to exact artifact hash/size.
