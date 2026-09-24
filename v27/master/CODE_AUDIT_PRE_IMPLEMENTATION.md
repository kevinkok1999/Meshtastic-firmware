# V27 FINAL PRE-CODE — Existing Code Audit

Status: DESIGN/REVIEW ONLY. This document decides what may be reused from already-coded V27 work.

## Audited references

- v27-rc1: immutable known-good RC reference
- v27-rc2-work: experimental research branch only
- v27-master-design: authoritative pre-code design

P1 Pro V8, V26, V19 and all earlier versions are immutable.

## Executive result

Do NOT merge v27-rc2-work wholesale.

The existing work contains several valuable components, but the final V27 product architecture changed materially after RC1:
- Internet is now route 1 for V27 peers;
- RF is route 2/fallback except where P1/legacy compatibility requires it;
- opportunistic unknown-open Wi-Fi is now allowed only through a V27-specific untrusted-network sandbox;
- production relay must be authenticated/failover-capable;
- stable identifiers/routes must scale beyond the RC protocol's 64-bit values;
- crash/reboot delivery semantics must be durable.

The correct implementation strategy is selective extraction into fresh implementation branches from the pinned RC1 baseline.

## KEEP — proven concepts/components

### P1/V26 compatibility guards
Keep:
- exact published P1 V8 RF contract checks;
- V26 source remains untouched;
- compile-time V27 guards.

Reason:
These protect backward compatibility and stop accidental RF drift.

### Fixed-size E2E encrypted envelope concept
Keep:
- AEAD authenticated encryption;
- fixed-size padded global messages;
- sender identity inside ciphertext;
- exact text length inside ciphertext;
- no plaintext channel secret.

Change:
Protocol version and identifier widths will change for Stable.

### Pairwise DM key derivation
Keep concept:
- recipient-specific shared secret;
- domain-separated key derivation;
- route capability not derivable from public key alone.

Review before Stable:
- formal domain labels;
- multi-device semantics;
- protocol-v3 context binding.

### Signed global group posts
Keep concept:
- sender device signature inside encrypted group payload;
- verify before UI insertion.

Change:
Bind signature to protocol version, conversation capability, 128-bit message ID, ordering data and payload.

### Cross-transport dedup
Keep concept:
- one logical message across RF + Internet;
- dedup before UI insertion.

Replace:
RC RAM-only 64-entry/64-bit implementation with protocol-v3 128-bit durable replay/idempotency window.

### Bounded resources
Keep:
- bounded device queues;
- bounded relay buffers;
- input rate guard;
- reconnect backoff with jitter;
- incremental main-loop subscription/contact processing.

Strengthen:
- memory governor;
- flash-wear limits;
- CPU/task budgets;
- large-contact scaling.

## REWRITE — required before Stable

### Transport orchestration
Current RC behavior:
- RF send is performed and global path mirrors it.
- group RF send occurs and Internet is mirrored.

Problem:
This conflicts with the final GLOBAL-FIRST product policy.

Stable behavior:
- V27 peer + GLOBAL_READY -> global route first;
- RF is fallback after bounded failure/timeout;
- P1/legacy -> RF immediately;
- mixed/unknown legacy groups retain compatibility behavior;
- same message ID across transitions.

Action:
Replace mirror-centric send semantics with a Transport Orchestrator. Do not patch the RC send path piecemeal.

### Production relay
Current RC1:
- public development broker, plaintext transport.

Current RC2 research:
- verified TLS on the public development broker.

Problem:
TLS alone does not provide production device authorization, ACLs, revocation, store-and-forward or deployment ownership.

Stable:
- dedicated authenticated relay;
- short-lived/revocable credentials;
- 128-bit opaque capabilities;
- no anonymous production access;
- two failure domains or equivalent tested failover;
- ciphertext-only bounded offline queue;
- delivery receipts.

RC2 TLS work is useful as a transport experiment, not production backend architecture.

### Global identifier width
Current:
- keyed 64-bit message ID;
- group route token is shorter than the DM route in the RC code.

Problem:
Worldwide scale and long-lived replay/idempotency need more collision margin and consistent capability size.

Stable:
- 128-bit logical message ID;
- 128-bit opaque route capability for DM and group;
- new protocol version rather than silently mutating RC v2.

### Replay/dedup persistence
Current:
- RAM-only dedup window.

Problem:
Reboot can forget recently delivered global envelopes and delayed copies may appear again.

Stable:
- bounded durable replay/idempotency journal;
- flash-write coalescing/compaction;
- TTL/epoch-based cleanup.

### Offline queue/delivery semantics
Current:
- bounded RAM mirror queues;
- no recipient-device delivery receipt;
- no production server store-and-forward.

Stable:
- crash-safe local journal;
- ciphertext-only server queue;
- relay accepted != recipient delivered;
- E2E/device-authenticated delivery receipt.

### Contact/topic lookup scaling
Current:
- pairwise subscriptions;
- incoming topic may be mapped by walking known contacts/routes.

Problem:
Linear contact/subscription scaling is not the desired worldwide architecture.

Stable:
- authenticated device inbox multiplexing or equivalent bounded subscription model;
- local capability map/cache;
- E2E conversation keys remain pairwise/channel-specific.

## RC2 EXPERIMENT ISSUES

### Wi-Fi security floor
RC2 research currently broadens password-network filtering to WPA-level too early.

Problem:
The master contract says modern secure behavior first.

Stable:
- profile A starts WPA2 or better;
- WPA/WPA2 mixed fallback exists only in the explicit legacy compatibility profile;
- WEP never enabled.

### Hardcoded country rescue
RC2 research contains an NL country-code rescue behavior.

Problem:
A worldwide platform needs an explicit regional policy rather than a global hardcoded country.

Stable:
- region policy is selected/validated for the shipping RF/Wi-Fi profile;
- EU/NL build may use EU/NL channel rules;
- other regional releases have separate allowed-channel policy;
- never silently switch to a foreign regulatory profile.

### Open Wi-Fi policy conflict
Older RC tests require the existing WadaMesh open-auto-join preference to stay off.

Decision:
KEEP that legacy preference off.

New behavior:
Implement a separate V27 Opportunistic Connectivity engine with:
- unknown-open candidate discovery;
- optional OWE where actually compiled;
- TLS/E2E-only untrusted sandbox;
- no LAN admin exposure;
- no permanent trust promotion;
- captive/invalid-TLS suppression;
- trusted Wi-Fi always higher priority.

This avoids reusing an unsandboxed legacy auto-join path.

### Wi-Fi link vs Internet
RC2's compatibility ladder is association-centric.

Stable requires separate states for:
- associated;
- DHCP/IP;
- DNS/route;
- captive/filtered;
- secure relay TLS;
- relay auth;
- GLOBAL_READY.

Do not tear down a healthy association simply because DHCP/DNS/relay failed.

## REMOVE / FORBID in Stable

- public anonymous broker as a production default;
- plaintext MQTT port fallback;
- setInsecure() TLS behavior;
- 64-bit stable message IDs;
- 64-bit stable group route capability;
- hardcoded world-wide NL country behavior;
- legacy unsandboxed unknown-open autojoin;
- false "Sent" based solely on hidden RAM queue acceptance;
- unbounded contact/topic subscriptions;
- logs containing message body, keys, Wi-Fi password, route secrets or relay credentials.

## ADD — not present in RC1/RC2

- protocol-v3 capability negotiation;
- crash-safe message journal;
- recipient-device delivery receipts;
- authenticated multi-region/failover relay;
- unknown-sender request/quarantine;
- block/mute independent of transport;
- group membership/key epoch design for future controlled private groups;
- signed OTA + rollback;
- network reset separate from ownership reset;
- identity recovery/transfer design;
- local reset/panic/watchdog diagnostics;
- secure production credential lifecycle;
- region policy layer;
- Master Gateway capability;
- advanced V27-only RF intelligence lane;
- compatibility-lane isolation for P1/legacy.

## Branch decision

- v27-rc1: KEEP IMMUTABLE.
- v27-rc2-work: QUARANTINE AS RESEARCH; surgical fixes are allowed but it is not a merge source.
- v27-master-design: DESIGN SOURCE OF TRUTH.
- next code branch: create fresh from v27-rc1 after design freeze and selectively port approved components.

## Final pre-code gate

Coding may begin only if:
1. master design contradictions are zero;
2. this keep/rewrite matrix is accepted;
3. protocol-v3 invariants are frozen;
4. global-first route semantics are frozen;
5. Wi-Fi trusted/opportunistic sandbox states are frozen;
6. region policy is explicit;
7. P1/V26 immutability tests exist;
8. no production feature depends on the public broker.
