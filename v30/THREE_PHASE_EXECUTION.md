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

## Current checkpoint — 2026-11-17
This is a gated development checkpoint, NOT a release candidate.

- Phase 1 baseline reconstruction and host/embedded build passed at `2db9785` (run `43978496645`), including application headroom. F1 actual dispatcher still not activated; opt-in completion hook candidate validated on exact pinned Dispatcher source blobs in run `43980854722`. No physical airtime, TX, or recipient ACK proof.
- Structural 184-byte envelope parser: RED `43978441855`, GREEN `43978558814`; integrated. Crypto authentication, nonce, and actual trust path remain open.
- Airtime governor, bounded journal, private-key normalization/capability derivation, normalized MeshMap builder and installer validator: combined host CI `43980017941` success on `fca2940`. Journal rejects corrupt/uncertain slots and generation exhaustion; no actual flash storage backend or hardware tests. Channel helper is not proof of native/browser live join.
- Browser frame patch: RED `43980331956`, GREEN `43980500817`, applied to V30 reconstructed checkout and tested in central host CI `43980628630` on `38f39fc`. Session authorization/pairing remains OPEN; do not pass channel secrets into unpaired browser WS.
- Isolated world-relay core (scoped grant contract, origin-bound idempotency and pagination) passed run `43980761231`. Does not authenticate clients or persist data; not deployed.
- No real nationwide signed map pack, native geometry renderer, production relay connection, mixed transports, HIL, RF conformity, 72h soak or 192h external-power evidence. No earlier firmware sources or release artifacts were changed; V29 branch stays at `18b9f4d4c00c372b5d458c4fbb8c9779f80c6b91`.

All three phases remain OPEN. Development branches and host tests are valuable progress, but not a substitute for phase completion gates. Never re-label a development build as a stable customer product.

### Updated integration checkpoint (2026-11-17)

- Phase 1: bounded file journal, actual portal request/confirmation wiring and dormant radio-admission bridge; candidate full opt-in Dispatcher build 44184780932 passed. No actual airtime/receive/ACK or storage power-cut proof, no new RF mode enabled.
- Phase 2: exact same-key native chat join/open (44184356058), raw-map startup guard (44184306946), bounded map reader, native national-source ingestion smoke (44185369352), signed installer and exact partition validation, localhost two independent clients with registered-channel challenge grants. Integrated host suite 44185369383 passed on 5f560c59. No fully measured or signed national map, native map rendering, public/TLS relay, native internet session, or browser pairing activation.
- Phase 3: development firmware compilation succeeded for preceding integration and isolated candidates; combined newest firmware build is pending. No hardware access, conformity, 72h/192h measurements or production signing. All phases remain open. Never distribute development firmware as a customer end product.

### 2026-11-18 checkpoint
Combined 718f9688 software + actual pinned T-Deck build passed (44185700985, 44185701037). Map viewport worker and actual packet-pool observer isolated builds passed (44185658673, 44185727514) and are integrated as unactivated development modules. Map national build requires fresh verified pack output; old run failed province alias, fixed on current branch. Browser C++11 repair remains isolated. No physical validation, public relay or signed map; all phase gates remain open.
