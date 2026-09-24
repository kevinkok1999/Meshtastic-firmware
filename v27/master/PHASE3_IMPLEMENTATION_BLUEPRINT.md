# V27 MASTER PROJECT — FASE 3: IMPLEMENTATION BLUEPRINT & RELEASE PROGRAM

Status: DESIGN ONLY — this is the implementation map, not implementation.

## 1. Dependency order

No parallel coding across incompatible layers.

### Wave 0 — Freeze contracts
Owners:
- Architecture Master
- Security Master
- Validation Master

Deliverables:
- interface versions
- state enums
- message envelope schema
- queue limits
- Wi-Fi state machine
- relay auth contract
- Master Gateway boundary
- P1 V8 invariants

No code until Wave 0 signed off.

### Wave 0A — Existing-code quarantine and extraction
Owners:
- Architecture Master
- Firmware Master
- Security Master
- Validation Master

Actions:
- keep v27-rc1 immutable as known-good reference;
- keep v27-rc2-work explicitly experimental;
- extract proven components only through reviewed patches;
- do not merge RC2 wholesale;
- add tests that older/P1 source trees remain byte-for-byte untouched by V27 work.

Exit gate:
- keep/rewrite matrix approved;
- no hidden dependency on public broker or RC2 Wi-Fi assumptions.

### Wave 1 — Embedded foundations
Owners:
- Firmware Master
- Wi-Fi Master
- RF Master

Implement:
- V27 compile guards
- Transport Orchestrator with GLOBAL-FIRST policy for V27 peers and RF-second/fallback semantics
- capability registry / safe legacy detection
- 128-bit logical message identity core
- crash-safe bounded message journal
- Internet health state separate from Wi-Fi link
- deterministic Wi-Fi compatibility ladder
- bounded retry/backoff
- existing RF path untouched

Exit gate:
- compile
- static contracts
- P1 RF profile unchanged
- no UI blocking

### Wave 2 — Secure global transport + production relay contract
Owners:
- Security Master
- Relay Master
- Firmware Master

Implement:
- verified TLS
- device authentication
- short-lived credential lifecycle
- opaque route authorization
- fixed-size encrypted protocol-v3 envelopes
- 128-bit opaque route capabilities
- authenticated multi-region/failover relay endpoint contract
- no insecure or anonymous production fallback

Exit gate:
- invalid certificate rejected
- invalid credential rejected
- RF continues during all failures

### Wave 3 — Delivery, offline behavior & abuse resistance
Owners:
- Relay Master
- Data Master
- Firmware Master
- UX Master

Implement:
- delivery receipts
- bounded server-side ciphertext queue
- TTL cleanup
- idempotent replay
- accurate UI state mapping
- recipient-device delivery receipts
- unknown-sender request/quarantine flow
- device + relay rate limits and quotas
- block/mute semantics independent of transport

Exit gate:
- no duplicates
- no false Sent
- backend outage leaves RF fully usable

### Wave 4 — Master Gateway
Owners:
- Network Master
- Security Master
- UX Master
- Validation Master

Implement separately from T-Deck firmware:
- 5-GHz/Ethernet WAN
- 2.4-GHz LAN/AP
- NAT/DHCP/DNS
- secure provisioning
- automatic upstream recovery

Exit gate:
- T-Deck requires no gateway-specific chat code
- removing gateway simply causes RF/global failover

### Wave 5 — Secure lifecycle + installer / product integration
Owners:
- Installer Master
- UX Master
- Validation Master

Implement:
- RC/full-flash artifacts
- manifest
- checksums
- signed firmware verification
- rollback-capable update path
- anti-rollback production policy
- separate network reset vs ownership reset
- website installer
- clear RC vs Stable labeling

## 2. Engineering review model

Every code change needs review from:
- subsystem owner;
- one adjacent-subsystem reviewer;
- validation owner for test impact.

Security-sensitive changes also need Security Master signoff.

Examples:
Wi-Fi change:
- Wi-Fi Master
- Firmware Master
- Validation Master

Relay crypto change:
- Security Master
- Relay Master
- Firmware Master
- Validation Master

RF change:
- RF Master
- Firmware Master
- P1 compatibility gate
- Validation Master

## 3. Git branch program

Reference:
- v26-rf-intelligence: untouched baseline
- v27-rc1: pinned proven RC
- v27-master-design: design-only source of truth

After design signoff:
- v27-impl-wave0-extract
- v27-impl-wave1
- v27-impl-wave2
- v27-impl-wave3
- gateway-v1-design / gateway-v1-impl
- v27-release-candidate

Do not code directly on v27-master-design.

## 4. CI program

Cheap gates first:
1. design/contract marker checks
2. patch determinism
3. P1 RF invariants
4. security static checks
5. compile
6. artifact integrity

Hardware gates after compile:
- T-Deck A/B
- P1 V8
- Wi-Fi router matrix
- two unrelated Internet uplinks
- gateway
- failure injection

## 5. Wi-Fi validation matrix

Minimum real equipment classes:
- modern ISP dual-band router
- older 2.4-GHz router
- WPA2-only AP
- WPA2/WPA3 transition AP
- WPA3-capable AP
- Android hotspot
- iPhone hotspot where available
- mesh/extender
- channel 12/13 AP
- hidden SSID
- Master Gateway downstream AP

Record:
- scan visibility
- association time
- auth result
- DHCP time
- reconnect behavior
- disconnect reason
- heap watermark
- watchdog/reset count

## 6. Messaging validation matrix

DM:
- V27 A -> V27 B global only
- RF only
- hybrid duplicate
- Internet loss mid-send
- reboot/reconnect
- revoked relay auth

Group:
- global
- RF
- hybrid duplicate
- signed sender verification
- malformed signature
- removed channel
- queue replay

Legacy:
- V27 <-> P1 V8
- V27 <-> legacy RF peer
- Internet on/off must not alter RF compatibility

## 7. Master Gateway validation

- 5-GHz WAN -> 2.4-GHz LAN
- Ethernet WAN -> 2.4-GHz LAN
- WAN loss
- WAN return
- DHCP renew
- DNS failure
- router reboot
- T-Deck remains locally usable via RF throughout

## 8. Soak / resource gates

T-Deck:
- long-duration Wi-Fi + BLE + LVGL + LoRa
- repeated connect/disconnect
- message bursts
- queue pressure
- contact/channel changes

Measure:
- heap minimum
- PSRAM minimum
- task watchdog
- loop latency
- RF receive degradation
- battery impact
- reconnect rate

No Stable if:
- watchdog reset
- progressive heap loss
- RF starvation
- UI freeze
- reconnect storm

## 9. Worldwide resilience gates

Before Stable:
- at least two independent relay failure domains or an equivalently tested automatic failover architecture;
- one region/failure domain can disappear without losing local RF operation or corrupting message state;
- device chooses healthy endpoint without user action;
- ciphertext queues remain idempotent across failover;
- no plaintext social graph is required for message routing;
- relay health outages cannot trigger Wi-Fi reconnect storms.

## 10. Production backend gates

Before Stable:
- authenticated relay
- verified TLS
- credential revocation
- no public anonymous broker dependency
- RLS/policy validation
- ciphertext-only offline store
- TTL cleanup
- quotas
- abuse/rate limits
- audit without plaintext

## 11. Release ladder

1. Design Approved
2. Engineering Dev
3. CI Green
4. Bench Alpha
5. Two-Network Beta
6. P1 Compatibility Beta
7. Gateway Beta
8. Soak Passed
9. Limited Canary
10. V27 Stable

V26/RC1 remain rollback until step 10.

## 12. Definition of "fully functional"

V27 may only be called fully functional when:
- ordinary user needs no transport configuration;
- broad 2.4-GHz router matrix passes;
- optional 5-GHz Master Gateway works transparently;
- global DM works across unrelated networks;
- #groups work across unrelated networks;
- RF fallback works with cloud entirely offline;
- P1 V8 bidirectional tests pass;
- no duplicate visible messages;
- delivery state is accurate;
- verified authenticated production relay with failover is live;
- opportunistic open Wi-Fi sandbox passes malicious/invalid-TLS tests;
- signed update + rollback recovery is validated;
- crash/reboot duplicate suppression is validated;
- hardware soak passes;
- installer and rollback are validated.

Anything less is RC/Beta, not Stable.
