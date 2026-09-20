# Coding Handoff — MeshOffGridNL Version 7

Work on the existing project. This is a V7 implementation, not a rewrite of historical versions.

## Mission
Implement MeshOffGridNL Version 7 as a Saitama v1.3.0-derived T-Deck Plus firmware that preserves standard MeshCore/LoRa operation and adds ESP-NOW Long Range as an optional V7-to-V7 direct transport with AUTO fallback.

## Read first
- v7/UPSTREAM.lock.json
- v7/ARCHITECTURE.md
- v7/IMPLEMENTATION_PLAN.md
- v7/TEST_MATRIX.md
- v7/CI_DESIGN.md
- v7/TEAM_COORDINATION.md
- v7/RELEASE_CHECKLIST.md

## Hard constraints
- Never edit/delete/rebase historical V1-V6 branches/releases.
- Do not use Meshtastic V6 source as the V7 firmware base.
- Import exact Saitama v1.3.0 commit 16598b4e6a7eabd6195d06d2a3abc1b3b448f368.
- Pin MeshCore d92964352441e53b93e8667b802e04f6e072b39e.
- Preserve GPL-3.0-or-later licensing.
- Keep 16MB DIO T-Deck Plus target.
- Preserve MeshCore interoperability over LoRa.
- No plaintext downgrade of private chat.
- One firmware runner: no build matrices or parallel firmware jobs.
- Do not publish V7 to the MeshOffGridNL production site until release gates pass.

## Exact first coding sequence
1. Create isolated dev branch from this prep branch.
2. Import Saitama source with history/provenance documented; initialize pinned MeshCore submodule.
3. Reproduce unmodified baseline build before feature coding.
4. Add tests/mocks and transport interfaces.
5. Implement EspNowLRTransport without changing UI.
6. Implement identity binding + protected payload seam.
7. Implement HybridTransportManager and AUTO fallback/dedup.
8. Add append-only config migration and UI/diagnostics.
9. Run unit/static tests.
10. Run exactly one integrated t-deck firmware build.
11. Execute two-device hardware matrix.
12. Only after gates pass create immutable V7 release artifacts.
13. Then create a separate website feature branch for V7 installer preview.
14. Verify Vercel preview + real browser flash before production promotion.

## ESP-NOW requirements
- use ESP32-S3 Wi-Fi station lifecycle appropriate to Saitama;
- enable WIFI_PROTOCOL_LR;
- use ESP-NOW peers deliberately; do not rely on implicit broadcast behavior;
- application ACK/sequence semantics are required;
- fragment conservatively because MeshCore frames can reach 255 bytes while ESP-NOW peer capabilities vary;
- static/bounded queues and reassembly;
- robust malformed-frame handling;
- record retries/fallback/dedup metrics;
- test BLE coexistence on hardware.

## Definition of done
“Build succeeded” is not done. V7 is done only when:
- LORA_ONLY regression passes,
- V7-to-V7 ESP-NOW LR path passes,
- AUTO fallback passes,
- private-message protection is preserved,
- stock MeshCore interoperability over LoRa passes,
- merged image is verified/booted,
- release hash+size are pinned,
- website preview flashes a real T-Deck Plus successfully,
- production publication is a separate final gated action.

If evidence contradicts this design, stop the affected implementation path, document the finding, and choose the smallest architecture change that preserves the invariants.
