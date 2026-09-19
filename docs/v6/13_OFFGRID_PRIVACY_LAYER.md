# V6 Off-Grid Privacy Layer — Architecture Draft

## Goal
Protect not only message content, but also reduce metadata leakage on LoRa, ESP-NOW, XBee and local store-carry-forward paths.

V6 MUST NOT claim RF invisibility. A nearby receiver can still detect that radio energy/traffic exists.

## Layering

Plaintext
-> V6 end-to-end encryption
-> sender-sealed secure envelope
-> rotating transport/discovery identifiers
-> optional route-privacy layer
-> LoRa / ESP-NOW / XBee / local relay

## 1. Mandatory E2EE
All V6-native off-grid messages remain end-to-end encrypted.
Relays never need plaintext.

## 2. Rotating Radio Identities
Do not broadcast one permanent human/device identifier forever.

Separate:
- long-term device root identity;
- per-contact pseudonym;
- short-lived discovery identifier;
- transport session identifier.

Rotation must not break verified-contact recognition.

## 3. Sender-Sealed Envelopes
Where routing permits, keep sender identity inside the protected payload.
A relay should only need:
- opaque destination/routing token;
- expiry;
- bounded forwarding metadata;
- encrypted envelope.

## 4. Pairwise Contact Pseudonyms
Contact A and Contact B should not necessarily observe the same external identifier for the same device.
This reduces easy cross-contact correlation.

## 5. Route Privacy
Research a bounded layered-forwarding mode for high-privacy off-grid traffic.

Concept:
sender constructs a short permitted relay path or privacy path.
Each relay learns only enough to forward toward the next step, not the entire route.

Do not invent custom onion cryptography.
If implemented, use reviewed layered-encryption constructions/libraries and benchmark packet overhead.

Because LoRa payloads are small, route privacy must have strict limits:
- short paths;
- compact headers;
- no unbounded nested encryption;
- explicit airtime budget.

## 6. Opportunistic Relay Privacy
Store-carry-forward relays receive opaque custody objects.

Relay should not know:
- plaintext;
- contact display name;
- full contact graph;
- exact original sender identity if avoidable.

Relay may know:
- ciphertext size class;
- expiry;
- opaque destination token;
- custody/retry state.

## 7. Metadata-Minimized ACKs
Receipts should not reveal unnecessary identity or message semantics.
Use opaque message references and protected receipt content where possible.

## 8. Padding Classes
Use a small number of size classes to reduce exact message-length leakage.

Example concept:
small / medium / large protected frames.

Do not pad every tiny LoRa message to a huge fixed size; airtime and regulation matter.
Choose classes through RF/airtime benchmarks.

## 9. Discovery Privacy
Nearby discovery should use rotating pseudonymous beacons.

A passive observer should not see:
"Kevin-TDeck"

Instead:
short-lived opaque discovery value.

Verified contacts can resolve allowed rotating values using shared/contact state.

## 10. No Global Presence
Off-grid discovery is local and temporary.
No permanent public "online now" beacon tied to long-term identity.

## 11. Replay & Tracking Resistance
Old discovery frames and old envelopes must not remain valid indefinitely.
Use:
- epochs;
- expiries;
- replay cache;
- authenticated freshness data.

Avoid precise global timestamps when coarse epochs are sufficient.

## 12. Optional High-Privacy Relay Mode
User-facing privacy profile can enable:
- stronger identifier rotation;
- stricter padding;
- route privacy if available;
- no public discovery;
- manual/verified-contact discovery only;
- shorter relay retention.

Tradeoff:
more latency and/or airtime.

## 13. RF Fingerprinting Limits
Firmware cannot guarantee protection from sophisticated RF fingerprinting or direction finding.
Do not claim otherwise.

Possible mitigations are limited:
- avoid unnecessary periodic beacons;
- randomize nonessential transmission timing within bounded windows;
- rotate logical identifiers;
- minimize unique protocol quirks.

Do not use continuous cover traffic by default.

## 14. Transport-Specific Notes

### LoRa
Pros:
- long range;
- no Internet metadata.

Privacy concerns:
- RF observability;
- stable node IDs;
- timing;
- packet size;
- repeated relay patterns.

### ESP-NOW
Pros:
- local peer-to-peer;
- no access point required.

Privacy concerns:
- Wi-Fi MAC/link-layer identifiers;
- channel-level observability;
- peer discovery metadata.

V6 should investigate randomized/scoped transport addressing where compatible with ESP-NOW constraints.

### XBee
Pros:
- independent local radio path.

Privacy concerns:
- module addressing;
- transport metadata;
- persistent hardware identifiers depending on configuration.

Wrap V6 E2EE above XBee regardless of module-level security.

## 15. Off-Grid Privacy Profiles

BALANCED:
- E2EE
- rotating discovery IDs
- sender-sealed envelope where supported
- normal padding

PRIVATE:
- E2EE
- pairwise pseudonyms
- stricter identifier rotation
- minimal discovery
- shorter relay retention
- stronger padding classes

MAXIMUM PRIVACY:
- E2EE
- no public discovery
- verified-contact discovery only
- route privacy where available
- strict metadata budget
- bounded timing jitter
- shortest practical relay retention

## 16. UX

Normal screen:
Privacy: Maximum
Connection: Off-grid
Status: Encrypted

Advanced details:
- discovery mode
- identifier rotation
- route privacy
- relay retention
- padding profile

Do not expose raw keys or hardware addresses in normal UI.

## 17. Testing
Mandatory tests:
- identifier rotates on schedule;
- verified contacts still resolve rotating IDs;
- old IDs expire;
- duplicate/replayed beacon rejected;
- relay cannot decrypt payload;
- sender identity absent from clear relay header where design requires;
- OFF_GRID emits zero Internet traffic;
- privacy profile survives reboot;
- route privacy failure does not silently downgrade if profile requires it.

## Key principle
Internet privacy and off-grid privacy are different problems.

Internet privacy focuses on:
IP, DNS, gateways, relays and server metadata.

Off-grid privacy focuses on:
RF observability, stable radio identifiers, packet timing/size, relay knowledge and route correlation.

V6 needs both.
