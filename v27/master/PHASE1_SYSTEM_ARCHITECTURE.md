# V27 MASTER PROJECT — FASE 1: SYSTEM ARCHITECTURE & ENGINEERING BRANCHES

Status: DESIGN ONLY — no firmware implementation is authorized from this branch.

Pinned proven baseline:
- V27 RC1 commit: 07caaf0b64d34410b53b8d8540ea8eb3ab1b7ec6
- V26 remains untouched and independently releasable.
- v27-rc2-work is research material only and is NOT the source of truth.

## Master-program structure

V27 is treated as one product with multiple engineering workstreams. Every workstream owns a bounded subsystem and must publish an interface contract before implementation.

### Branch A — Firmware / Embedded Master
Owns:
- T-Deck ESP32-S3 runtime
- transport orchestrator
- Wi-Fi state machine
- queueing, dedup, retry/backoff
- RAM/flash/task-safety
- existing MeshCore integration
- P1 Pro V8 compatibility

Must not:
- redefine cloud auth semantics
- retune P1/LoRa RF profile
- bypass security contracts
- expose transport complexity in normal UI

### Branch B — RF / Mesh Master
Owns:
- LoRa/MeshCore radio invariants
- P1 Pro V8 interoperability
- local fallback behavior
- airtime/load analysis
- interference coexistence with Wi-Fi/BLE
- RF regression matrix

Pinned P1 V8 RF:
- 869.618 MHz
- 62.5 kHz
- SF8
- CR5
- 22 dBm ceiling

### Branch C — Wi-Fi / Connectivity Master
Owns:
- ESP32-S3 2.4-GHz compatibility
- WPA2/WPA3/PMF behavior
- channels 1–13 for NL/EU build
- hidden SSID handling
- DHCP/DNS/Internet-health differentiation
- roaming/mesh/extender behavior
- reconnect ownership
- captive/no-Internet detection

Hardware truth:
- T-Deck ESP32-S3 is 2.4-GHz Wi-Fi only.
- 5 GHz is not added by firmware.

### Branch D — Master Gateway / Network Master
Owns optional external gateway:
- upstream Ethernet / 5-GHz Wi-Fi / optional 2.4-GHz Wi-Fi
- downstream 2.4-GHz AP for T-Decks
- NAT/DHCP/DNS
- downstream WPA2/WPA3 compatibility
- gateway health/failover
- no chat decryption
- no private device-key custody

The T-Deck treats the Master Gateway as an ordinary 2.4-GHz network.

### Branch E — Security / Cryptography Master
Owns:
- threat model
- E2E message crypto
- key derivation
- device identity
- group signatures
- metadata-minimization
- replay protection
- route-capability design
- credential lifecycle
- certificate/TLS requirements
- log-redaction rules

Security team can block release independently.

### Branch F — Relay / Backend Master
Owns:
- authenticated global relay
- device sessions
- ACL/capability enforcement
- bounded encrypted store-and-forward
- delivery receipts
- rate limits / abuse controls
- TTL cleanup
- observability without plaintext

Public anonymous broker is DEVELOPMENT ONLY.

### Branch G — Control Plane / Data Master
Primary candidate: Supabase or equivalent.

Owns:
- device registry
- credential issuance/revocation
- route/capability metadata
- encrypted envelope metadata
- delivery receipts
- RLS / policy design
- audit and cleanup jobs

Neon is NOT in the realtime critical path. It can later serve analytics, test-data, cold export, reporting.

### Branch H — UX / Product Master
Owns one-message-product experience:
- one chat timeline
- same DM/#group regardless of transport
- no MQTT/broker/LoRa selector in normal flow
- human-facing status only
- setup/onboarding
- failure wording
- advanced diagnostics separation

### Branch I — Installer / Release Master
Owns:
- clean/full flash vs app update
- version manifest
- checksums
- rollback
- website installer
- release channels: dev -> rc -> canary -> stable
- P1 Pro V8 compatibility matrix

### Branch J — Validation / Reliability Master
Independent from implementers.

Owns:
- contract tests
- hardware tests
- two-network Internet tests
- Wi-Fi router matrix
- P1 V8 RF tests
- soak tests
- heap/PSRAM/watchdog metrics
- failure injection
- release go/no-go

## System architecture

User action:
1. compose one logical message;
2. one stable logical message-id;
3. Chat Core submits to Transport Orchestrator;
4. Transport Orchestrator selects available path(s);
5. dedup occurs before UI insertion;
6. delivery state is transport-independent.

Transport paths:
- RF/MeshCore
- P1 Pro V8 RF infrastructure
- Wi-Fi -> Global Relay
- Wi-Fi -> optional Master Gateway -> Internet -> Global Relay

No path is allowed to become mandatory for local messaging.

## Transport Orchestrator states

- OFFGRID_ONLY
- WIFI_CONNECTING
- WIFI_LOCAL_ONLY
- INTERNET_CHECKING
- GLOBAL_CONNECTING
- HYBRID_READY
- GLOBAL_READY
- GLOBAL_BACKOFF
- DEGRADED

Input signals:
- Wi-Fi association
- IP/DHCP
- DNS
- relay TLS
- relay authentication
- RF availability
- recipient capability
- queue pressure
- battery/resource state

## Hard architectural invariants

1. V26 is never edited by V27.
2. V27 is layered on RC1/proven V19/V26 behavior.
3. P1 V8 RF parameters cannot be changed by Wi-Fi/global work.
4. Wi-Fi failure cannot block RF loops.
5. Relay/backend failure cannot block RF loops.
6. Master Gateway is optional.
7. Global transport is additive; local RF remains independently valid.
8. One user message == one visible logical message.
9. Duplicate transport delivery cannot create duplicate bubbles.
10. No cloud secret/service-role key in firmware.
11. Relay never needs plaintext message body.
12. Normal users do not configure transport internals.
13. A failed Internet path never makes a valid RF send fail.
14. A failed RF path never makes a valid global send fail for V27 peers.
15. If both fail, UI must not falsely report Sent.

## Cross-team interface contracts

Firmware <-> RF:
- fixed RF profile
- shared message identity/dedup contract
- no Internet-origin flood to RF unless explicit gateway mode exists later

Firmware <-> Wi-Fi:
- event-driven state
- one reconnect owner
- no scan during association
- bounded retries
- no blocking waits on UI/radio task

Firmware <-> Relay:
- fixed envelope format
- explicit version
- exact max size
- authenticated connection state
- bounded queues
- idempotent message ID

Firmware <-> UX:
- transport-independent send API
- transport-independent delivery state
- diagnostics behind expert surface only

Relay <-> Control Plane:
- short-lived scoped credentials
- opaque route ACL
- ciphertext-only queue
- delivery receipt contract
- TTL and quotas

Gateway <-> T-Deck:
- standard 2.4-GHz IP networking only
- no proprietary chat coupling required

## Design-exit criteria for Fase 1

Fase 1 is complete only when:
- every workstream boundary is explicit;
- every single point of failure has a fallback;
- P1 V8 compatibility is preserved by design;
- 5-GHz limitation is honestly separated into the external Master Gateway;
- no subsystem requires the user to select a transport;
- security and validation branches can independently veto release.
