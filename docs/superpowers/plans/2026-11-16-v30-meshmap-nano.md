# V30 MeshMap Nano Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a nationwide offline base map inside existing internal flash, without SD.
**Architecture:** Preprocess licensed geography off-device into compact indexed vectors and search strings; bounded native renderer draws real features and labels. Separate immutable map pack and optional cache; preserve original app and user partition layout.
**Tech Stack:** Python 3.12 geodata preprocessing, OSM/PDOK, C++/LVGL, LittleFS, PlatformIO
**Spec:** `v30/MASTER_SPEC.md`

## Global Constraints
- Preserve V29 artifacts, native chat, P1 V8 baseline and existing flash partition offsets. Stock T-Deck Plus, no mandatory SD, Internet or phone.
- No production publication or unsupported claims; CI evidence is not HIL, RF compliance, runtime or commercial approval.
- Use fresh failing executable tests first; independently verify full applicable suite. Use exact baseline pins in spec.

## Global map constraints
- Tiles partition 0x4C0000 at 0x7D0000. Pack target <=3.5 MiB (3670016 bytes), cache <=512 KiB (524288 bytes), >=320 KiB free after actual filesystem overhead. Shrink data before changing partition; never remove A/B OTA or user storage.
- Geographical scope European Netherlands all 12 provinces including Wadden islands; publish exact polygon and source cut date. Real detail target z15–16 where data permits, viewport zoom 3–19, distinguish missing detail and overzoom. No guaranteed all streets/buildings/addresses. 
- No public tile-server scrape/Google redistribution; OSM ODbL provenance/attribution and separate source layers/licenses. Map files never over LoRa. No SD, network, phone or GPS required to view national default.
## Review Focus
- Whole-country pack exceeds capacity or silently drops province.
- Invalid offset/count/decompression or corrupt partition; no destructive autoformat.
- Map render delays radio/GPS; user data never migrated into SD.
- Zoom displays sparse data as detailed; no-fix/stale location and consent.
- Installer interrupted, wrong board/partition, OTA map intact.
---
### Task M1 — Data and budget experiment
**Files:** Create `v30/maps/build_nl.py`, `v30/maps/nl_profile.json`, `v30/maps/README.md`, `v30/tests/test_map_build.py`, `v30/maps/coverage.schema.json`.
**Interfaces:** `build_pack(source_path,profile,out_path)->BuildReport` with bytes, per-province roads by class/truncated features, places, zoom inventories, extent and data-date.
- [ ] Test synthetic fixture includes all defined features; deterministic encoding, invalid coordinates, duplicated strings, impossible budget and missing province rejected. `python -m unittest discover -s v30/tests -p 'test_map_build.py'` RED.
- [ ] Implement source extract workflow (Geofabrik licensed download/PDOK bulk), quantized delta/varint geometry, multiresolution simplification and spatial cells, shared name dictionaries, compressed bounded chunks. Prioritize country outlines/place names/motorways/primary roads, secondary/tertiary and selected local roads by measured budget. Report omitted classes and coverage. No false labels or invented data.
- [ ] Run fixture then actual Netherlands build (record source SHA, exact profile, time, size) and tune until <=3670016 bytes; if not enough, degrade feature selection visibly, not budget. No large binary in git; versioned verified artifact after build. Commit.
### Task M2 — Pack parser and storage
**Files:** `v30/overlay/src/ui-touch/NanoMapPack.{h,cpp}`, `NanoMapRepository.{h,cpp}`, `v30/tests/test_map_pack.cpp`.
**Interfaces:** `open(pack,manifest)->PackStatus`, `query(bounds,zoom,limit)->FeatureChunk`, `lookupPlace(prefix,limit)->PlaceResult`.
- [ ] Test corrupt magic/offset/size/count/string, truncated chunks, untrusted manifest, out-of-bounds coordinates, pack missing and cache full, stable internal map namespace; RED.
- [ ] Implement bounded indexed read, version, hash/signature manifest (signing key external), 512 KiB cache max, no implicit format on mount failure. Remain usable without a card. Separate internal user data.
- [ ] Host tests/fuzz malformed pack and actual LittleFS image size/free-space verification; commit.
### Task M3 — Native renderer and GPS
**Files:** `NanoMapRenderer.{h,cpp}`, `NanoMapLocation.{h,cpp}`, UI patches in `v30/apply_v30.py`, `v30/tests/test_map_view.cpp`.
**Interfaces:** `renderSlice(viewport,budget,lease)->RenderStatus`, `setZoom(z)`, `locationView(fix,age,accuracy)->LocationState`.
- [ ] Test viewport zoom 3–19, actual detail vs overzoom flags, Netherlands default, stale/no fix, place lookup, waypoints, radio-deadline yield; RED.
- [ ] Implement LVGL cooperative drawing; bounded per-cell vertex/label budgets, progressive rendering; markers and waypoint UI, attribution; no automatic broadcasts. Integrate R3 bus/preemption. Add optional consent-based local raster cache only after core, no implicit Internet calls.
- [ ] Benchmark actual 320x240 unit during rapid pan and RF traffic: cold first tile/geometry p95 <=500ms, viewport <=2s target, warm pan p95 <=150ms; report any misses, never silently drop P0. Commit.
### Task M4 — Installer / pack lifecycle / validation
**Files:** `v30/maps/manifest.schema.json`, `v30/maps/pack_image.py`, `v30/qa/maps-matrix.md`, V30-only validation workflow and later separately gated website integration.
**Interfaces:** manifest binds version, board, partition offset/size, file hashes, source dates, extent, credits; update verify/activate/recover.
- [ ] Test wrong offset/board, invalid signature, interrupted pack update, pack missing/corrupt, firmware update leaves maps and identity; offline first boot without SD. RED.
- [ ] Generate verified read-only base map image; maintain last-good asset or recoverable fallback, no automatic erasure and no touching app/user partitions beyond explicitly authorized V30 install. USB/factory installer must use exact map offsets and verify readback, separate from regular V29 installer.
- [ ] Validate >=1000 stratified coordinates across all provinces/islands/border + coverage metrics, measured app/FS capacity; document road class/label omissions. Test no SD, no Wi-Fi, no GPS, map corruption and concurrent real RF. Keep production publication blocked until all F/R/M gates. Commit.
