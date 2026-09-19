# V6 Blind Internet Relay — Architecture Draft

Separate future repository recommended: MeshOffGridNL-V6-Relay.

## Goal
Provide global Internet delivery without the service being able to read chat content.

The relay is a delivery service, not a chat application.

## Language
Rust preferred for server implementation:
- memory-safe systems language;
- async networking ecosystem;
- strong type system;
- suitable for long-running concurrent service.

## External API
Use TLS.
The device uploads/downloads opaque encrypted envelopes.

Conceptual endpoints:
- publish envelope
- fetch mailbox batch
- acknowledge opaque envelope
- relay health/version

Do not expose raw SQL/database semantics to devices.

## Stored server data
Minimum:
- opaque mailbox/destination token
- opaque envelope bytes
- creation/expiry bucket
- delivery state
- abuse/rate-limit counters where required

Do not store:
- plaintext
- display names
- chat titles
- message previews
- precise user GPS
- contacts
- session private keys

## Retention
Undelivered objects expire automatically.
Delivered objects are deleted promptly according to policy.

Retention policy is visible to users/documentation.

## Mailbox identity
Do not equate public human username with server mailbox.

Mailbox tokens should be opaque and designed for rotation where protocol allows.

## Authentication
Device authorization must not require email/phone account for core messaging.
Design bearer/cryptographic authorization around device/contact protocol.

Do not make the relay a universal identity authority.

## Rate limiting
Needed to prevent resource exhaustion.
Use privacy-conscious limits:
- per opaque credential/mailbox;
- coarse IP protections at edge only where operationally required;
- avoid building unnecessary long-term tracking profiles.

## TLS
TLS protects transport to relay.
E2EE still protects message content from relay itself.
These are separate layers.

## Database
PostgreSQL is suitable for durable mailbox metadata/ciphertext initially.
Schema must not tempt application developers to add plaintext chat columns.

## Service separation
Recommended:
- API gateway/service
- delivery store
- cleanup/expiry worker
- metrics service with no chat content

## Metrics
Allowed examples:
- aggregate queue depth
- request latency
- error counts
- aggregate bytes

Avoid:
- per-user conversation analytics
- plaintext logging
- long-lived contact graph metrics

## Backups
Server backups contain only ciphertext/opaque metadata.
Retention mirrors production deletion policy where feasible.

## Multi-relay future
Protocol should not assume only one MeshOffGridNL server forever.
Future options:
- self-hosted relay
- organization relay
- multiple configured relays

Do not implement federation before basic semantics are stable.

## Threat boundary
Relay can still observe:
- client IP at network layer;
- connection timing;
- ciphertext size unless padded;
- opaque mailbox activity.

Documentation must state this honestly.
