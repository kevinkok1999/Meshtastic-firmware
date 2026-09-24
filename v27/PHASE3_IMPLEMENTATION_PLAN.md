# MeshOffGridNL V27 — Phase 3: Implementation and Validation Plan

## Implementation order

### 1. V27 compile-time isolation
Add `MESH_OFFGRIDNL_V27=1` only to the intended T-Deck/T-Deck Plus target.
Require the V26 marker at patch time so V27 cannot accidentally be built on the wrong baseline.

### 2. V27 Transport Coordinator
Introduce a small state machine that observes, but does not own, Wi-Fi and LoRa.

States:
- OFFGRID_ONLY
- INTERNET_CONNECTING
- HYBRID_READY
- INTERNET_BACKOFF
- DEGRADED

Inputs:
- Wi-Fi connected/disconnected
- relay TLS/MQTT status
- credential validity
- queue depth
- broker acknowledgment/error

The coordinator may schedule relay work but may never block mesh processing.

### 3. Secure relay connection
Create a V27-specific MQTT connection configuration:
- TLS only;
- verified CA/hostname;
- short-lived device credentials;
- bounded reconnect;
- exponential backoff + jitter;
- keepalive;
- clean error state;
- no `setInsecure()` in V27 relay code.

### 4. Uplink integration
Hook after the existing packet has been encrypted and assigned its MeshPacket ID.
Do not create a second chat message object.
Mirror only permitted chat packets.

### 5. Downlink integration
Accept only:
- valid ServiceEnvelope;
- valid authorized topic;
- valid size limits;
- valid packet fields;
- valid cryptographic/routing policy.

Mark `via_mqtt`, deduplicate, then inject into the existing receive path.

Default behavior:
- display locally;
- do not automatically retransmit to LoRa.

### 6. Delivery and UI
Keep the existing chat UI.
Add small transport states such as:
- mesh
- global
- delivered
- queued
- offline

Do not split user conversations into separate 'Wi-Fi chat' and 'LoRa chat' screens.

### 7. Supabase control plane
Before production:
- devices table + RLS;
- rooms + memberships;
- credential endpoint/Edge Function;
- revocation;
- optional encrypted offline queue;
- TTL cleanup;
- private Realtime channels for future web/mobile surfaces.

Use publishable keys only where appropriate. Service secrets stay server-side.

### 8. Offline sync
If enabled, sync only encrypted envelopes.
Protocol must be idempotent by message ID.
After successful local acceptance, send a receipt/ack and allow server cleanup according to retention policy.

## Test matrix

### Compile/static
- V26 marker required.
- V27 marker present only on intended target.
- fail build if V27 relay calls insecure TLS APIs.
- fail build if V27 changes LoRa modem settings.
- fail build if V27 adds plaintext payload logging.

### Unit tests
- message-ID dedup LoRa then MQTT;
- message-ID dedup MQTT then LoRa;
- queue full;
- malformed envelope;
- oversized payload;
- unauthorized topic;
- expired credential;
- certificate rejection;
- reconnect backoff;
- PKI DM acceptance;
- encrypted channel message acceptance;
- MQTT-origin packet is not re-uplinked.

### Hardware integration
Use at least two V27 T-Deck devices:
1. same room, LoRa + same Wi-Fi;
2. different Wi-Fi networks;
3. phone hotspot versus fixed AP;
4. sender Wi-Fi disabled mid-send;
5. recipient Wi-Fi disabled and restored;
6. broker unavailable;
7. Supabase unavailable;
8. wrong TLS certificate;
9. duplicate packet delivered by LoRa and Internet;
10. long reconnect cycle;
11. reboot with pending encrypted queue;
12. V27 to V26 local LoRa compatibility.

### Global acceptance test
Two devices on unrelated Internet connections with no usable LoRa path:
- join same authorized channel;
- exchange encrypted #/channel messages;
- exchange PKI/private messages;
- confirm single display per message;
- confirm delivery after reconnect where offline queue is enabled.

## Performance budgets
Set hard budgets before coding:
- no blocking network calls in mesh/UI loop;
- bounded relay RAM;
- bounded message queue;
- bounded dedup cache;
- no unbounded String growth;
- payload sizes remain compatible with existing ServiceEnvelope limits;
- reconnect cadence cannot starve Wi-Fi association or RF servicing.

## Rollout
1. internal V27 engineering build;
2. two-device local validation;
3. two-network validation;
4. forced-failure validation;
5. limited canary;
6. only then website installer release.

V26 remains available as rollback and is never overwritten.

## Release gates
V27 is not releasable until all are true:
- verified TLS;
- encrypted relay path;
- DM + channel global chat;
- no duplicate visible messages;
- LoRa works with backend fully offline;
- no automatic Internet-to-LoRa flooding;
- Supabase RLS/advisor checks clean;
- credentials revocable;
- hardware tests pass;
- website installer points to a validated V27 artifact only.
