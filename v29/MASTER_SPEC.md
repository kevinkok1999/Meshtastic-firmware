# MeshOffGridNL V29 — Dutch Emergency Fabric

## Mission
V29 turns the T-Deck Plus into a simple household emergency communications terminal that keeps its core functions working without Internet, cloud services or a Wi-Fi router.

V29 is additive on the proven V28 stack. V28 remains the compatibility baseline.

## Product rule
The user sees simple actions. The firmware handles routes, queues, retries, encryption, storage and power policy.

Emergency Home:
- Ik ben veilig
- Ik heb hulp nodig
- Stuur bericht
- Noodinformatie
- Gezin / verzamelpunt
- Netwerkstatus

Technical RF/route/memory details live only under Advanced.

## Hard safety/truthfulness rules
- A local mesh help request is never presented as a 112 call.
- V29 never impersonates NL-Alert or an official authority.
- Information always carries a trust class: Official, Verified Local, Trusted Contact, Community, Unverified.
- Internet is optional and never required for the V29 core.
- V29 does not jam, block or interfere with third-party RF users.
- Existing EU868/power/airtime constraints remain authoritative.

## 192-hour engineering target
V29 is engineered around an 8-day / 192-hour resilience target when used with an adequate external energy source.
This is a MeshOffGridNL engineering target, not a statement that Dutch government guidance requires eight days.

Power modes:
1. Normal
2. Emergency
3. Critical

Critical mode keeps essential receive/check-in/queued-delivery functions before optional display, Wi-Fi portal, GPS or rich UI work.

## Memory policy
The user requested aggressive memory use. V29 implements this safely:
- up to 80% of the safe PSRAM/cache/storage budget may be used dynamically;
- at least 20% remains reserve;
- internal DRAM safety margin always overrides the utilization target;
- caches are shed before message journals;
- emergency queues are bounded and priority aware;
- recovery/config space is never consumed by caches.

## Offline message fabric
V29-to-V29 nodes can use a signed, encrypted, delay-tolerant bundle layer above the existing RF stack.

Properties:
- random logical message ID;
- sender identity;
- recipient hint;
- encrypted recipient-bound payload;
- authenticated encryption;
- Ed25519 origin signature;
- fragmentation below MeshCore raw payload limits;
- deduplication;
- TTL/expiry;
- bounded relay attempts;
- store/carry/forward;
- priority classes;
- crash-safe persistence.

A moving node may carry an opaque encrypted bundle between disconnected mesh islands. Relay nodes never need plaintext.

## Priority classes
P0 — local emergency/help/check-in critical
P1 — direct household/person message
P2 — local coordination/bulletin
P3 — bulk/offline content metadata

Priority affects queue order and retry cadence, not legal RF limits.

## Storage hierarchy
1. Internal DRAM: only latency-critical runtime state.
2. PSRAM: reassembly, dedup, caches, queue indexes.
3. Internal SPIFFS: small durable emergency journal/configuration.
4. microSD when present: larger offline documents, map packs, manuals, bulletins and extended history.

No SD card must never disable core emergency messaging.

## Local phone portal
A later V29 phase adds an Emergency Local Portal:
- T-Deck can expose a local Wi-Fi network without Internet;
- nearby phones use a browser, no app required;
- simple check-in/help/message UI;
- portal cannot access device admin, private keys or channel keys;
- portal submissions enter the same V29 queue and trust model.

## Compatibility
- V28 chat remains functional.
- LoRa/RF remains the independent core transport.
- P1 Pro V8 compatibility lane remains unchanged.
- V29 enhancements are capability-gated.
- No second ordinary chat engine is introduced.

## Specialist release gates
V29 must pass five workstreams before release:
1. Embedded Memory & Offline Fabric (#16)
2. RF Mesh & Delay-Tolerant Routing (#17)
3. Security, Identity & Trust (#18)
4. Dutch Emergency UX & Local Portal (#19)
5. 192h Power, Storage & Recovery (#20)

A failure in any release gate blocks V29 publication.


## V29.2 resilience hardening
- Protocol v2 carries cumulative relay age so the 192-hour lifetime cannot reset at a new carrier.
- Cumulative age and remaining carry budget are mutable but normalized out of AEAD AAD and the origin signature.
- Carry budget is priority-aware: critical traffic may cross more carriers than bulk traffic.
- Relay nodes de-duplicate before recipient/carry routing.
- A single signed origin cannot retain more than eight critical objects per node; newest supersedes oldest.
- Portal actions Safe, Help, Moving and Meeting Point are POST-only, session-token protected and rate limited.
- GET captive-portal probes can render/status only and can never emit an emergency action.
- Double-buffer SPIFFS snapshots remain the reboot/brownout recovery path.
