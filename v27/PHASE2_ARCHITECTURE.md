# MeshOffGridNL V27 — Phase 2: Global Relay Architecture

## Architecture decision
Use the firmware's existing encrypted MQTT/ServiceEnvelope path as the device data plane. Use Supabase as the control plane, authorization source, optional encrypted offline queue, and web/mobile realtime layer.

Do not put a second database in the critical message path merely for redundancy. Neon can be used later for analytics, backup/export, or isolated load testing, but duplicating live message state between Supabase Postgres and Neon would add consistency and failure modes without improving on-device delivery.

## Why MQTT is the V27 device data plane
The current code already knows how to:
- publish encrypted MeshPackets;
- subscribe by channel global ID;
- handle PKI direct-message topic traffic;
- mark inbound packets `via_mqtt`;
- avoid re-publishing MQTT-originated packets;
- queue temporarily when disconnected;
- feed accepted packets back into the existing router and message UI.

This minimizes RAM, flash, protocol, and regression risk compared with implementing Supabase's full Realtime WebSocket/Phoenix protocol directly on the ESP32-S3.

## Logical topology

T-Deck V27
  -> existing Wi-Fi manager
  -> V27 Transport Coordinator
      -> LoRa/WadaMesh path (unchanged)
      -> verified TLS MQTT relay
  -> existing Router/TextMessageModule/MessageStore

Supabase control plane
  -> device registry
  -> device/public-key identity
  -> room/channel memberships
  -> credential issuance/revocation
  -> optional encrypted offline envelopes
  -> delivery metadata/receipts
  -> Realtime for future web/mobile clients and presence

Optional Neon role
  -> analytics or load-test dataset
  -> cold export/backup
  -> never required for device send/receive

## Security model

### End-to-end packet confidentiality
V27 should relay the already-encrypted Meshtastic packet rather than decrypt/re-encrypt chat text in the cloud.
- Channel chat uses the channel's existing encryption.
- Direct chat uses existing PKI encryption where available.
- Relay services handle opaque ciphertext plus minimal routing metadata.
- `moduleConfig.mqtt.encryption_enabled` must be required for the V27 worldwide relay path.

### TLS must be upgraded in V27
Current MQTT reconnect code uses `mqttClientTLS.setInsecure()`. V27 must not inherit this behavior for its private relay.

V27 requirement:
- verify server hostname;
- verify CA chain or a pinned trust anchor;
- fail closed on certificate errors;
- never silently downgrade to plaintext MQTT;
- store only non-secret trust material in firmware;
- rotate server certificates without firmware replacement where possible by anchoring to a stable CA.

### Device identity
Never embed a Supabase secret/service-role key in firmware.

Preferred flow:
1. Device has or derives a stable public-key identity from the existing Meshtastic/PKI identity.
2. One-time activation associates that identity with a MeshOffGridNL account/device record.
3. Device proves possession using a nonce/challenge.
4. Control plane issues short-lived relay credentials.
5. Revoked devices stop receiving new credentials.

If the chosen MQTT broker supports signed JWT authentication, issue short-lived JWTs. Otherwise issue per-device scoped credentials and rotate them.

### Authorization
A relay credential must be scoped to:
- one device identity;
- permitted channel topics;
- its DM receive topic/PKI identity;
- bounded expiry.

Do not let a device subscribe to `#` or arbitrary tenants.

### Supabase RLS
Use private Realtime channels and RLS for web/mobile clients and control-plane tables. Authorization is based on authenticated identity and explicit room membership.

## Data model proposal

### devices
- id
- owner_user_id
- node_num
- public_key
- display_name
- firmware_major
- revoked_at
- created_at
- last_seen_at

### rooms
- id
- global_channel_id
- name
- owner_user_id
- created_at

### room_members
- room_id
- device_id / user_id
- role
- joined_at

### encrypted_envelopes
Optional offline store only.
- message_id
- sender_device_id
- recipient_scope
- ciphertext/service_envelope
- created_at
- expires_at
- delivery_state

### delivery_receipts
- message_id
- recipient_device_id
- received_at
- transport

Store no plaintext chat body.

## Reliability

### Dual-path send
For normal V27 AUTO behavior, local packet generation remains unchanged. If Internet is healthy, mirror the same encrypted packet to MQTT. This provides:
- local LoRa operation;
- global reach;
- natural fallback;
- same packet identity for dedup.

### Loop prevention
- retain `via_mqtt` behavior;
- do not re-uplink an MQTT-originated packet;
- add V27 bounded message-ID cache covering both transports;
- default Internet ingress is local delivery only, not LoRa rebroadcast.

### Offline delivery
The broker may provide short transient buffering, but V27 reliability must not depend solely on retained MQTT.
For true store-and-forward, Supabase can keep encrypted envelopes with TTL and allow authenticated replay/sync after reconnect.

### Backpressure
- bounded RAM queue;
- drop oldest noncritical relay copy before affecting LoRa;
- exponential backoff with jitter;
- no reconnect storm;
- no blocking waits in UI/radio loops.

## Presence
Presence is optional and advisory only. It must never be used to decide whether a message is allowed to send. This avoids delivery failures caused by stale online/offline state.

## Privacy
- no automatic GPS/location upload;
- no plaintext telemetry in chat records;
- no map reporting enabled by V27;
- online status can be disabled by user;
- logging must redact tokens, passwords, payload plaintext, and full private keys.
