# V6 Native Wire Protocol — Draft 0

This document defines structure, not final cryptographic byte layout.

## Goals
- transport-independent;
- compact enough for constrained radios;
- versioned;
- deterministic parsing;
- fragmentation-aware;
- replay/dedup aware;
- opaque payload to relays;
- forward-compatible extensions with explicit size limits.

## Magic/version
All V6-native frames start with a small recognizable protocol discriminator and version.

Conceptual:
MOG6 | wire_version | flags | header_len | ...

Do not encode human-readable JSON on radio links.

## Logical identifiers

### MessageId
128 random bits generated from a cryptographically suitable RNG/DRBG.
Used for deduplication and receipts.

### ConversationId
Opaque identifier scoped to conversation semantics.
Must not expose a person's display name.

### DestinationToken
Routing-oriented opaque identifier.
Exact lifetime/rotation behavior belongs to the identity/routing design.

## SecureEnvelope conceptual fields

Header (minimal cleartext):
- wire_version
- envelope_id/message_id or bounded routing alias
- destination token
- expiry class / hop constraints
- fragment descriptor
- cipher-suite id
- protected length

Protected/authenticated section:
- sender/session identity data as required
- conversation id
- message type
- logical timestamp if needed
- plaintext body
- receipt/request metadata
- padding

Authentication:
- AEAD tag/signature material according to the selected suite.

## Message types
Initial bounded registry:
- TEXT
- DELIVERY_RECEIPT
- READ_RECEIPT
- SESSION_CONTROL
- CONTACT_CONTROL
- GROUP_CONTROL
- INVENTORY_SUMMARY
- STORE_FORWARD_CONTROL

Do not add image/audio/file transfer until text reliability is proven.

## Fragmentation
Fragment only the encrypted/protected object, never independently re-encrypt arbitrary plaintext slices unless the crypto design explicitly requires it.

Fragment descriptor includes bounded:
- object id
- fragment index
- fragment count
- protected total size / validation data as required

Hard limits:
- max fragments per object;
- max concurrent reassemblies;
- max total reassembly bytes;
- per-peer quotas;
- expiry for incomplete objects.

## Reassembly
A message is exposed to ConversationCore only after:
1. all required fragments arrive;
2. length bounds pass;
3. integrity/authentication passes;
4. replay/dedup checks pass;
5. decryption succeeds.

## Dedup
Dedup key includes MessageId plus protocol context.
Maintain RAM fast-set plus persistent bounded recent-message index where required.

## Receipts
Transport acceptance != end-to-end delivery.

Separate:
- transport accepted;
- relay custody accepted;
- destination delivered;
- destination read.

UI must never show "delivered" merely because a radio driver accepted bytes.

## Expiry
Use coarse expiration classes where possible to reduce metadata.
Expired ciphertext is deleted from queues/relay stores.

## Inventory Sync
Peers exchange compact summaries before bulk store-forward synchronization.
Candidates:
- bounded Bloom filter or other compact set reconciliation;
- recent time/epoch windows;
- explicit request for missing IDs.

False positives must only delay transfer, never corrupt state.

## Parser requirements
- reject unknown critical version;
- ignore/skippable optional extensions only when length-delimited and explicitly safe;
- integer overflow checks;
- exact upper bounds before allocation/copy;
- fuzz target for every decoder;
- no recursive parser structures;
- no untrusted length controlling stack arrays.

## Compatibility
V6 Native envelopes may be carried over a Meshtastic compatibility transport, but their internal V6 format remains independent.
