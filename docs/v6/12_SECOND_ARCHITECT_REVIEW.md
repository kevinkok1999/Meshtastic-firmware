# V6 Second Architect Review — Red-Team Architecture Pass

Status: PRE-CODING DESIGN REVIEW.

Purpose: challenge the first V6 architecture and add privacy, reliability, supply-chain and hardware ideas that were previously missing.

## Executive conclusion
The first architecture is strong on E2EE, transport abstraction, storage and a privacy tunnel. The second-architect review identifies four additional architectural themes:

1. Split knowledge across independent Internet components instead of trusting one VPN/gateway.
2. Stop using one global device identity everywhere; use scoped/pairwise pseudonyms.
3. Authorize service use without persistent account identifiers where practical.
4. Treat firmware provenance and update transparency as part of the privacy/security product.

---

## 1. Adopt: Split-Knowledge Internet Path

A VPN hides destinations from the local network but moves metadata visibility to the VPN gateway.

A stronger V6 Internet architecture can split metadata knowledge:

T-Deck
 -> Ingress/Oblivious Relay
 -> Privacy Gateway
 -> Blind Mailbox Relay
 -> recipient

Conceptual visibility:
- Ingress sees source network/IP but only an encrypted inner request.
- Privacy Gateway sees the inner service request but not the original client network identity.
- Blind Mailbox Relay stores opaque end-to-end-encrypted envelopes.

This borrows the architectural principle of Oblivious HTTP/privacy partitioning: no single service should need the complete client + request picture.

V6 application E2EE remains inside all of these layers.

Decision: ADOPT AS TARGET ARCHITECTURE.
V6.0 may ship a simpler one-gateway path first, but wire/API design must not prevent split-knowledge deployment.

---

## 2. Adopt: Scoped / Pairwise Identities

Do not expose one permanent V6 node identifier to:
- every contact;
- every relay;
- every discovery beacon;
- every transport.

Instead separate identities:

DeviceRootIdentity
 -> ContactPseudonym(contact A)
 -> ContactPseudonym(contact B)
 -> RelayMailboxToken(relay X)
 -> DiscoveryEpochId(local epoch)
 -> TunnelCredential(gateway Y)

A compromise/correlation at one layer should not automatically reveal the same stable identifier at every other layer.

Requirements:
- contact A and contact B should not necessarily see the same transport identifier;
- server mailbox token is not the contact public identity;
- discovery IDs rotate;
- display name is never the cryptographic primary key.

Decision: ADOPT.

---

## 3. Adopt: Sender-Sealed Delivery

Relays usually need to know where an envelope goes, but should not need the human/long-term identity of who sent it.

Native V6 envelope should aim for:
- opaque destination token visible enough for routing;
- sender/contact identity inside the end-to-end protected section;
- relay learns as little sender identity as practical.

Relays may still infer network origin from transport metadata; split-knowledge architecture mitigates but does not eliminate this.

Decision: ADOPT.

---

## 4. Research/Adopt: Privacy-Preserving Service Authorization

The Blind Relay needs anti-abuse/rate-limit controls, but normal account cookies/device IDs create tracking.

Research privacy-preserving authorization tokens inspired by Privacy Pass:
- service can verify that a valid authorization token exists;
- redeemed tokens are designed to be unlinkable to issuance where possible;
- avoids a permanent account identifier on every mailbox request.

Potential uses:
- relay rate-limit quota;
- paid/service entitlement without exposing chat identity;
- temporary gateway access tokens;
- abuse prevention.

Never design a custom anonymous-token primitive.

Decision: RESEARCH FOR V6 INTERNET RELAY; ADOPT ONLY WITH REVIEWED IMPLEMENTATION.

---

## 5. Adopt: Application Oblivious Requests Before Full VPN Complexity

Alternative path to a full device VPN:

V6 application request
 -> HPKE/oblivious encapsulation
 -> relay
 -> gateway
 -> mailbox service

This can reduce correlation without turning the entire ESP32 network stack into a generic VPN client.

Possible advantage:
- smaller attack surface;
- clear application-only policy;
- fewer DNS leak paths;
- easier fail-closed semantics.

The current Privacy Tunnel architecture remains valid, but this is now a peer design option.

Decision:
Benchmark three Internet privacy paths before coding final transport:
A. direct E2EE + TLS
B. application oblivious relay/gateway
C. full/tunnel VPN

Choose based on RAM, battery, latency, implementation maturity and privacy benefit.

---

## 6. Research: MASQUE-Style Proxying

MASQUE CONNECT-UDP provides a standardized way to proxy UDP through HTTP infrastructure.

Potential future use:
- tunnel UDP-based application traffic through an HTTP privacy proxy;
- run QUIC-like transport behind a privacy gateway;
- integrate with an application privacy tunnel.

This is not automatically simpler than WireGuard on ESP32-S3.

Decision: RESEARCH ONLY. Do not make V6.0 depend on MASQUE before an embedded prototype proves value.

---

## 7. Adopt: Capability-Based Internal Security Boundaries

Current embedded projects often rely on convention: every module can access every global.

V6 should model permissions explicitly.

Examples:
- Transport module: can read encrypted envelope bytes, never plaintext.
- Crypto module: can access plaintext/keys, has no direct radio API.
- UI: can request send/decrypt-view operations, cannot extract raw private keys.
- Storage worker: stores encrypted records, cannot initiate network sends.
- Diagnostics: receives redacted events only.

Even inside one C++ firmware image, architecture should enforce narrow interfaces.

Decision: ADOPT.

---

## 8. Adopt: Formal Model Before Complex Delivery/Crypto State Machines

V6 combines:
- offline delivery;
- multiple transports;
- replay protection;
- ratcheting;
- power loss;
- store-carry-forward;
- receipts.

Some bugs will be state-machine bugs, not C++ bugs.

Before implementation of the final protocol, model critical invariants in a formal/specification tool such as TLA+ for distributed/delivery state.

Candidate invariants:
- a delivered message is never reverted to undelivered;
- expired custody never resurrects;
- a message key is never reused after committed send;
- OFF_GRID never selects Internet transport;
- TUNNEL_REQUIRED never uses direct relay;
- no custody loop continues indefinitely beyond limits;
- duplicate envelope never creates duplicate user message.

Cryptographic protocol design can additionally be reviewed with a dedicated cryptographic protocol analysis approach/tool.

Decision: ADOPT FOR CRITICAL STATE MACHINES.

---

## 9. Adopt: Build Provenance + Reproducible Release Goal

A privacy device is only trustworthy if users can trust the firmware binary.

V6 release metadata should include:
- exact source commit;
- dependency/toolchain versions;
- build environment;
- partition map;
- SHA-256;
- firmware signature;
- machine-verifiable build provenance;
- SBOM where practical.

Long-term target:
independent rebuild of the same source should reproduce the same firmware artifact or a documented reproducibility boundary.

Adopt SLSA-style provenance concepts rather than relying only on "GitHub Action succeeded".

Decision: ADOPT.

---

## 10. Adopt: Update Transparency / Anti-Silent-Targeting

A signed update can still be malicious if the signing authority intentionally serves one special user a different signed binary.

Future architecture should support a transparency concept:
- release manifests are publicly auditable;
- stable release digest is published consistently;
- device can display build hash;
- website/download hash matches device-reported build;
- no silent per-device production firmware variants.

For sensitive environments, require a release to exist in the public transparency record before auto-update.

Decision: ADOPT AS RELEASE ARCHITECTURE GOAL.

---

## 11. Adopt: Physical Privacy as a Future Hardware Revision

Firmware privacy cannot fully compensate for sensors/radios the user cannot physically disable.

Future MeshOffGrid hardware revision should investigate physical switches for:
- microphone power/data;
- GPS power;
- Wi-Fi/BLE radio policy where electrically practical;
- camera if future hardware ever has one.

A physical switch state should be readable/displayed but not overrideable by software.

Optional secure element should be evaluated for:
- long-term device key protection;
- signed challenge operations;
- manufacturing identity.

Do not require new hardware for V6 software, but design APIs so a secure element can be added later.

Decision: FUTURE HARDWARE TRACK.

---

## 12. Adopt: Pairing Ceremony With Human-Verifiable Meaning

QR pairing should not merely copy a key.

Pairing ceremony can establish:
- verified identity;
- per-contact pseudonym;
- initial session/bootstrap material;
- optional allowed transport policy for that contact;
- optional trust label.

UI should show a short human-verifiable phrase/emoji code in addition to QR.

Decision: ADOPT.

---

## 13. Research: Internet Batching and Timing Privacy

Even padded ciphertext can leak timing patterns.

For Internet mode only, the gateway could optionally:
- batch messages into short windows;
- normalize envelope size classes;
- add bounded random dispatch jitter;
- combine multiple queued envelopes.

Tradeoffs:
- increased latency;
- more battery/network use;
- still not protection against a global traffic-analysis adversary.

Do NOT generate constant RF cover traffic on LoRa by default; it wastes battery/airtime and may conflict with regulatory duty-cycle constraints.

Decision: OPTIONAL PRIVACY PROFILE; INTERNET PATH ONLY.

---

## 14. Adopt: No Global "Online" Presence

Central online/offline status creates a tracking service.

V6 should default to:
- no public global presence;
- presence inferred only within an active conversation/session or recent permitted peer encounter;
- optional per-contact presence sharing.

Decision: ADOPT.

---

## 15. Adopt: Contact Graph Minimization

The Blind Relay must never maintain a plaintext "who talks to whom" graph.

Mailbox design:
- per-contact/per-session opaque tokens;
- rotating where feasible;
- relay stores destination mailbox relation needed for delivery only;
- avoid sender field where sender-sealed delivery works.

Decision: ADOPT.

---

## 16. Research: Redundant Fragment Coding Instead of Blind Retries

For very lossy off-grid links, repeated retransmission is not always optimal.

Research optional erasure/FEC-style redundancy for fragmented objects:
- sender creates N data fragments + small redundancy;
- receiver reconstructs once enough fragments arrive;
- can reduce retry chatter on lossy links.

Use only after airtime/CPU benchmarks.
Text messages are small; this may be more useful for future larger V6 objects.

Decision: RESEARCH, NOT V6.0 REQUIREMENT.

---

## 17. Adopt: Trust Zones / Security Profiles

Instead of hundreds of toggles, expose a few coherent policies:

BALANCED
- E2EE
- normal discovery
- direct Internet allowed

PRIVATE
- E2EE
- rotating identifiers
- tunnel/oblivious path preferred
- minimal metadata/logs

MAXIMUM PRIVACY
- fail-closed protected Internet path
- no public presence
- stronger batching/padding where practical
- location disabled
- stricter retention

OFF_GRID
- no Internet path at all

These are policies, not different cryptographic quality levels. E2EE remains mandatory across all modes.

Decision: ADOPT.

---

## 18. Adopt: Privacy Budget Dashboard for Developers

Each module declares what metadata it emits.

Example machine-readable design inventory:

InternetTransport:
- destination class: privacy-sensitive
- IP-visible-to: ingress
- persistent identifier: none
- timestamps: coarse
- logs: aggregate only

This makes privacy review repeatable instead of relying on memory.

Decision: ADOPT.

---

## 19. Reject: "Untraceable Mode" Marketing

No architecture can honestly guarantee untraceability against every observer.

Use accurate claims:
- end-to-end encrypted;
- metadata-minimized;
- off-grid capable;
- optional privacy tunnel;
- split-knowledge relay architecture;
- no required phone/email identity.

Decision: EXPLICITLY REJECT "UNTRACEABLE" CLAIM.

---

## 20. Proposed Revised Internet Architecture

High-privacy target:

T-Deck
  [message E2EE]
     |
  local Internet
     |
  optional Privacy Tunnel / ingress relay
     |      sees source transport metadata
     v
  Oblivious Privacy Gateway
     |      does not receive original client IP in split design
     v
  Blind Mailbox Relay
     |      stores opaque V6 envelope
     v
  recipient fetch path

Separate identities:
- contact identity
- discovery identifier
- tunnel credential
- authorization token
- mailbox token

No single identifier should join all of these datasets by default.

---

## 21. Updated Top-Level Priorities

P0:
- E2EE architecture
- pairwise/scoped identities
- secure storage
- mode enforcement
- fail-closed privacy policy
- split-knowledge-compatible Internet API
- reproducible/provenanced release design

P1:
- store-carry-forward
- inventory sync
- blind relay
- application oblivious path
- privacy-preserving authorization
- formal delivery model

P2:
- full WireGuard/MASQUE research
- timing batching
- FEC fragment redundancy
- secure-element hardware revision
- multi-provider/federated relays

---

## Second Architect Verdict

V6 should no longer be conceptualized as a "privacy phone with a VPN".

It should be:

A transport-independent, end-to-end encrypted, metadata-minimized communication system in which identity, network origin, routing, authorization and mailbox delivery are deliberately separated so no single infrastructure component needs the full communication graph.
