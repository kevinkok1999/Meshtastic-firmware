# V30 PRO — three-phase delivery and verification
User-approved 2026-11-17. This file orders the already approved implementation tasks; it does not replace MASTER_SPEC.md, GLOBAL_CHAT_AND_UX.md or any requirement. Only three outward phases. No release until every required gate is verified.

## Phase 1 — communication and reliability foundation
F1 actual direct/raw path, admission, real TX completion and retries; F2 durable inbox/outbox/receipt; F3 envelope and trust; F4 portal session; R1 physical airtime; R2 route intelligence and R3 bus/power. F5 integration checks run after R1; no ungoverned real RF. G1 inherited functionality inventory feeds all phases. Gate: firmware reconstruction, pinned dependency, complete host tests and actual embedded build, explicit ACK/queue/replay/power and airtime evidence. Phase is OPEN.

## Phase 2 — complete product capabilities
M1–M4 nationwide internal map pipeline, validated storage, native rendering/GPS, signed recovery/install. R4 supported multi-transport, G2 shared channel key + stable logical identity, G3 browser Chats, G4 worldwide HTTPS relay and durable sync including separately versioned emergency envelopes, G5 transport interoperability. Preserve all earlier features; unsupported hardware explicitly marked. Gate: all actual map data generated/fit/verified; key join and browser work; encrypted messages over two independent connections, failure/recovery, and all supported transport path tests. Phase is OPEN.

## Phase 3 — integrated end product
F5/R3/G6 and full acceptance matrix A1–A9: firmware and assets, exact flash/readback/rollback, P1 compatibility, UX no-dead-ends, wallpaper asset and physical rendering, 72h mixed soak, 192h external-energy measurement, RF legality, installer and product copy consistency. Release candidate only after independently verified gates. No website publication or production release until user approves release candidate and hardware evidence is present. Phase is OPEN.

## Execution protocol
One task implementer writes at a time on the isolated V30 branch; parallel read-only specialist preflight and independent task review. No repeated plan-only phases. Each task adds actual code + reproducible test, logs the exact commit/run and evidence; fix failing tests before closing. Do not call simulated/host test RF or physical test; hardware/backend unavailable is a documented release blocker, not approval to skip. Preserve prior version artifacts.

## Current checkpoint
2026-11-17: F1 host adapter checkpoint 823a48a, compatibility fix 8fd6402, explicit payload bound 51814b3. GitHub Actions runs 43976865574, 43977067896, 43977155723 completed successfully (host transport validation only). Actual production dispatcher admission/TX callback, full PlatformIO build and HIL remain unverified; independent review underway. No phase is closed.
