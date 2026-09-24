# MeshOffGridNL V27 — Phase 1: Baseline and Invariants

## Status
Design-only phase. No V26 source code is changed.

## Baseline
V27 is branched from `v26-rf-intelligence`. V26 remains the proven RF/Wi-Fi baseline and must stay independently releasable.

## Product goal
Allow V27 devices to exchange normal chat messages worldwide whenever Internet connectivity is available, while preserving the existing LoRa/off-grid experience when Internet is absent.

Required user experience:
- Same chat UI for local and worldwide messaging.
- Existing channel/# messages continue to work.
- Existing private/PKI direct messages continue to work.
- Wi-Fi available: an encrypted copy can use the worldwide relay.
- Wi-Fi unavailable: LoRa works exactly as before.
- Cloud outage: LoRa works exactly as before.
- No manual mode switching required in AUTO mode.
- Sender sees transport/delivery state without exposing implementation complexity.

## Non-negotiable invariants
1. Do not modify or rewrite the V26 branch.
2. V27 changes are gated behind `MESH_OFFGRIDNL_V27`.
3. No V27 feature may retune LoRa frequency, bandwidth, spreading factor, coding rate, TX power, AGC policy, or V26 RF guard behavior.
4. No V27 failure may block `the_mesh.loop()` or the UI loop.
5. Internet transport must be optional and fail closed.
6. A cloud credential may never grant database-admin/service-role access.
7. Private-message plaintext must not be stored by the relay.
8. Internet-originated packets must not automatically flood LoRa. Internet-to-LoRa bridging is a separate explicit gateway mode, disabled by default.
9. The existing V19 Wi-Fi association/recovery path remains authoritative.
10. Firmware rollback to V26 must remain possible with the existing installer strategy.

## Existing capabilities confirmed in the codebase
The current firmware already contains:
- Wi-Fi station/reconnect handling.
- MQTT networking and queueing.
- TLS-capable MQTT client support.
- Meshtastic ServiceEnvelope encoding.
- Encrypted MQTT uplink/downlink.
- Channel-global identifiers.
- PKI direct-message handling over MQTT.
- `via_mqtt` loop suppression.
- Existing message history and packet-ID dedup primitives.

This means V27 should extend existing transport instead of inventing a second incompatible chat stack.

## Target transport behavior
### Channel / # chat
A locally generated channel packet follows the normal LoRa path. If worldwide relay is enabled and Internet is healthy, the same encrypted packet is mirrored to the V27 relay using the channel global ID.

### Private DM
A PKI-encrypted direct packet follows the normal mesh path and can also be mirrored to the global relay. A V27 recipient anywhere in the world can receive it if online and authorized.

### Deduplication
The same logical MeshPacket ID is preserved across LoRa and Internet. Receiving the same packet by both transports must produce one visible message.

### Fallback
Internet transport is never a prerequisite for message creation or local LoRa transmission.

## Definition of Phase 1 complete
- Separate V27 branch exists.
- V26 is untouched.
- Requirements and hard safety invariants are frozen before implementation.
