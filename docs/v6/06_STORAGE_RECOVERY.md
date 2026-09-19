# V6 Storage, Persistence & Recovery

## Objectives
- encrypted at rest;
- power-loss safe;
- bounded;
- no plaintext logs;
- no silent corruption;
- recoverable without exposing secrets.

## Storage domains
Keep separate logical stores:
1. IdentityStore
2. ContactStore
3. SessionStateStore
4. MessageStore
5. DeliveryQueue
6. RelayCustodyStore
7. SettingsStore
8. DiagnosticCounters

Different domains can have different retention and encryption keys.

## Transaction model
Critical state updates use atomic/journaled semantics.

Example direct-message send commit:
1. derive/use next session message key;
2. produce ciphertext;
3. transactionally commit new ratchet state + ciphertext queue record;
4. only then make it eligible for transmission.

A reset between steps must not cause message-key reuse.

## Power-loss invariants
After reboot, each critical transaction resolves to:
- old valid state; OR
- new valid state.

Never half-updated ratchet state.

## Monotonic state
Security counters/epochs that must never roll backward require explicit strategy.
Do not assume filesystem write ordering is enough.

## Local message database
Record:
- encrypted message body;
- conversation reference;
- delivery state;
- local UI metadata;
- expiry/retention;
- authenticated integrity metadata.

Display names may be stored encrypted at rest.

## Storage encryption
Separate data-encryption keys by domain where practical.
A lost session key must not automatically reveal all historical local storage.

## Retention
User-facing choices:
- Keep messages
- Delete after N days
- Manual delete

Relay ciphertext has a much shorter automatic retention than endpoint history.

No hidden indefinite relay archive.

## Relay storage
Relay only stores:
- opaque envelope
- expiry
- retry/custody state

No generated plaintext index based on message content.

## Queue bounds
Every persistent queue has:
- max entries;
- max bytes;
- eviction policy;
- priority rules;
- expiry.

When full:
- never silently drop a high-priority user message without UI state;
- relay traffic may evict lower-priority relay traffic according to policy.

## Corruption handling
Each store:
- versioned schema;
- checksums/authentication;
- recovery scan;
- quarantine invalid records;
- no automatic overwrite of a corrupted encrypted baseline with defaults.

## Recovery mode
If normal boot cannot safely load state:
Recovery UI offers:
- retry mount/load;
- export privacy-safe diagnostics;
- restore firmware;
- restore encrypted identity backup;
- factory reset.

Do not expose secret material in recovery logs.

## Factory reset
Factory reset should:
- delete indexes;
- invalidate/destroy storage encryption keys;
- clear credentials;
- clear sessions;
- clear relay queues.

Explain that flash wear-leveling prevents a promise of physical bit-by-bit erasure.

## Crash dumps
Developer build:
- diagnostics allowed under explicit development policy.

Secure production:
- minimize or disable dumps containing sensitive RAM;
- never upload crash dumps automatically;
- user-controlled diagnostic export only.

## Migration
Every persistent schema has version and migration tests.
A firmware upgrade must never reinterpret old encrypted records without an explicit migration path.

## Backup model
Identity recovery package:
- encrypted/authenticated;
- user-controlled export;
- no server plaintext recovery key.

Conversation/session restore:
- not automatic by default;
- stale ratchet state is dangerous;
- restored identity should establish fresh sessions.

## Test cases
- power loss at every commit boundary;
- filesystem full;
- single-bit record corruption;
- truncated file;
- duplicate transaction;
- rollback attempt;
- version downgrade;
- interrupted migration;
- battery removal during message send;
- recovery after 1,000+ queued ciphertext records.
