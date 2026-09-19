# V6 Pre-Coding Gate

No large V6 implementation begins until these decisions are closed.

## A. Product
[ ] Final modes: Off-grid / Internet / Smart
[ ] Is Smart included in V6.0 or later?
[ ] Direct messages first; groups in V6.0 or later?
[ ] Text-only V6.0 scope confirmed
[ ] Message retention defaults
[ ] Relay retention defaults
[ ] Battery profiles

## B. Protocol
[ ] Wire version 1 field layout
[ ] MessageId construction
[ ] Destination token lifecycle
[ ] Fragment size/limits
[ ] Max logical message size
[ ] Expiry encoding
[ ] Receipt semantics
[ ] Inventory-sync algorithm
[ ] Dedup persistence window
[ ] Compatibility framing

## C. Crypto
[ ] Selected reviewed implementation/library
[ ] Direct-session bootstrap design
[ ] Ratchet design/implementation choice
[ ] Crypto suite registry
[ ] Identity key type
[ ] Contact verification encoding
[ ] Key-change behavior
[ ] Group strategy
[ ] PQ benchmark plan
[ ] Local storage key hierarchy
[ ] Backup/recovery key design

## D. Privacy
[ ] Cleartext metadata budget approved field-by-field
[ ] Discovery identifier design reviewed
[ ] Logging policy enforced in code structure
[ ] Location default OFF verified
[ ] Analytics policy
[ ] Server retention policy
[ ] Documentation wording avoids "untraceable" claims

## E. Embedded architecture
[ ] Task ownership diagram
[ ] Queue sizes
[ ] RAM budgets
[ ] Flash/partition budget
[ ] Storage transaction design
[ ] Watchdog behavior
[ ] Recovery mode
[ ] Developer vs secure-production build configs

## F. Transport
[ ] Transport interface frozen
[ ] LoRa adapter mapping
[ ] ESP-NOW adapter mapping
[ ] XBee adapter mapping
[ ] Internet adapter mapping
[ ] Mode enforcement tests
[ ] Congestion/airtime policy

## G. UX
[ ] 15 core screens approved
[ ] Error-state copy approved
[ ] Key-change warning UX
[ ] Offline queue UX
[ ] Recovery UX
[ ] Advanced settings boundary
[ ] NL/EN/DE string architecture

## H. Privacy Tunnel
[ ] V6.0 chooses application-scoped tunnel or full IP VPN
[ ] WireGuard/lwIP candidate benchmarked if full VPN is considered
[ ] Required/Preferred/Off semantics frozen
[ ] Fail-closed kill-switch tests defined
[ ] DNS/bootstrap leak model defined
[ ] Gateway identity/key lifecycle defined
[ ] Gateway/Blind Relay separation decision
[ ] IPv4/IPv6 and captive-portal behavior
[ ] Battery/keepalive resource budget
[ ] Self-hosted gateway policy

## I. Backend
[ ] Relay API
[ ] Opaque mailbox design
[ ] Database schema
[ ] Rate-limit model
[ ] TLS/certificate lifecycle
[ ] Deployment/update strategy
[ ] Deletion/retention jobs
[ ] Server observability privacy review

## J. Testing
[ ] Unit framework
[ ] Property testing
[ ] Fuzz harness
[ ] Network simulator
[ ] Power-loss harness
[ ] HIL matrix
[ ] Privacy assertions
[ ] Performance budgets
[ ] Release gates

## K. Production hardening
[ ] Signing key custody procedure
[ ] Signed update format
[ ] A/B OTA/rollback design
[ ] Secure Boot plan
[ ] Flash Encryption plan
[ ] eFuse provisioning checklist
[ ] Recovery path proven before irreversible locking
[ ] Manufacturing/provisioning dry run

## Definition of Ready
V6 coding may start in vertical slices only when the relevant section above is closed.

Recommended implementation sequence:
1. V6 MessageId + wire codec, host-only tests.
2. MessageStore transaction model, host-only tests.
3. CryptoProvider test adapter and vectors.
4. One direct V6 transport adapter using existing LoRa path.
5. End-to-end direct text message between two dev T-Decks.
6. Delivery receipts/dedup.
7. Store-carry-forward.
8. ESP-NOW/XBee adapters.
9. Internet blind relay.
10. UX integration.
11. Group messaging.
12. Secure-production hardening.

Do not start by implementing every transport at once.
