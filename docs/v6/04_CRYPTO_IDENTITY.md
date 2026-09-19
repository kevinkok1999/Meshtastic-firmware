# V6 Crypto & Identity Architecture — Draft

## Principle
Do not invent cryptographic primitives. V6 defines protocol composition and key lifecycle around reviewed implementations.

## Crypto abstraction
All crypto goes through CryptoProvider.

Interface responsibilities:
- randomBytes()
- identityGenerate()
- identitySign()/verify()
- sessionEstablish()
- messageProtect()/messageOpen()
- keyDerive()
- secureEraseBestEffort()
- suiteCapabilities()

Application code never calls low-level primitives directly.

## Crypto agility
Wire messages carry a bounded CryptoSuite ID.
This permits migration without changing the entire protocol.

Initial design targets:
- classical audited suite for V6.0;
- optional experimental hybrid/PQ suite only after device benchmarks and interoperability tests;
- unknown/disabled suites fail closed.

## Identity
A device creates a local cryptographic identity during first setup.

User-facing identity:
- display name is local/application data;
- cryptographic identity is separate;
- no phone number/email is required.

Contact record:
- contact id;
- current verified identity fingerprint;
- verification state;
- key-change history;
- optional user alias.

## Contact verification
Primary UX: QR scan while devices/users can compare in person.
Fallback: human-readable fingerprint/code.

States:
UNVERIFIED
VERIFIED
KEY_CHANGED
BLOCKED

KEY_CHANGED must be visible and must not silently inherit VERIFIED.

## Direct-message sessions
Design around a ratcheting session model:
- unique message keys;
- forward-secrecy-oriented key evolution;
- bounded skipped-message-key cache for out-of-order delivery;
- replay rejection;
- session reset/recovery protocol.

Do not copy a specification incompletely. Choose an existing reviewed implementation or a deliberately scoped protocol after benchmarking.

## Offline-first session establishment
V6 must support peers that are not simultaneously online.
Pre-key/session-init material may be transported by:
- QR/contact exchange;
- V6 relays;
- Internet blind relay;
- off-grid store-forward.

Server/relay must not receive private identity/session secrets.

## Group messaging
Group crypto is behind GroupCryptoProvider.

Preferred research path:
- benchmark an MLS-capable implementation on ESP32-S3;
- measure flash, RAM, handshake bytes and CPU;
- do not ship MLS simply because it is a standard if resource cost is unacceptable;
- do not replace MLS with an ad-hoc "almost MLS" protocol without explicit review.

Group membership changes create a new security epoch.
Removed members must not automatically receive future group secrets.

## Post-quantum preparedness
V6 protocol is crypto-agile from day one.

FIPS 203 ML-KEM is a standardized KEM, but V6.0 does not automatically enable ML-KEM over constrained radio.
Before any PQ suite:
- benchmark key/ciphertext sizes;
- measure LoRa airtime;
- measure RAM/flash;
- measure handshake CPU/energy;
- test hybrid composition.

Possible policy:
- Internet session bootstrap can adopt a hybrid suite earlier;
- constrained LoRa bootstrap may remain classical until a practical reviewed design is validated.

## Key hierarchy
Conceptual:
Device root/identity
 -> contact/session bootstrap
 -> session root
 -> send/receive chain keys
 -> per-message keys

Storage keys are separate from messaging keys.

Never reuse:
- firmware signing key;
- server TLS key;
- device identity key;
- local storage encryption key;
- session message key.

## Local key storage
Developer profile:
- recoverable test keys;
- no irreversible hardening.

Secure production profile:
- use ESP32-S3 security capabilities where appropriate;
- protect long-term key material with encrypted storage/hardware-backed strategy;
- pair Secure Boot with Flash Encryption after provisioning design is frozen.

## Key deletion
Flash wear-leveling means "delete file" is not a guarantee of physical overwrite.
Prefer crypto-shredding:
- encrypted records;
- destroy/rotate relevant data-encryption key;
- expire index references;
- garbage-collect encrypted remnants.

Do not claim forensic impossibility.

## Backups
Identity backup and chat/session backup are different products.

Recommended:
- optional encrypted identity recovery package;
- restoring identity does not silently restore stale ratchet state;
- conversation sessions renegotiate after recovery;
- backup format versioned and authenticated.

## Firmware trust
Secure-production:
- signed images only;
- private firmware signing key never committed to GitHub;
- release manifests contain public verification material, signature and hashes;
- rollback policy explicitly defined.

## Mandatory crypto tests
- official/library test vectors;
- deterministic protocol vectors;
- replay;
- duplicate;
- out-of-order;
- skipped-key bounds;
- corrupted AEAD;
- key change;
- session reset;
- group member add/remove;
- backup restore;
- power loss during state commit.
