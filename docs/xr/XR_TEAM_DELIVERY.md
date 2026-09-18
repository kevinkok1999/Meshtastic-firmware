# XR Team Delivery

MeshOffGrid XR keeps normal Meshtastic/LoRa as the compatible primary network and treats ESP-NOW and XBee XR 868 as cooperating secondary transports.

## User-facing rule

The user sends one message once. The firmware decides which available radios help. The user never chooses LoRa, ESP-NOW or XBee manually.

## Delivery model

1. LoRa/Meshtastic remains the primary send and the authority for end-to-end ACK/NAK.
2. At most one secondary transport may make an immediate best-effort assist copy for the same packet.
3. If reliable LoRa exhausts its retries, both sidecars may retain the same already-encrypted MeshPacket in bounded store/carry/forward storage.
4. The shared XRTransportTeam chooses only one recovery transport at a time from fresh route reports and link scores.
5. A local carrier success is not considered final delivery. The team waits for the real Meshtastic end-to-end ACK.
6. If that ACK does not arrive, the last accepted path gets a short cooldown so another healthy transport gets the next opportunity.
7. An ACK or explicit remote NAK cancels recovery state across the team.
8. Sidecar ingress is re-injected into the normal Meshtastic Router, so a packet can cross one transport and continue over LoRa without changing message identity.

## Why this is not a fixed LoRa -> ESP-NOW -> XBee chain

Range and RF conditions vary. A strong direct ESP-NOW peer can be useful at short range, while XBee may be the better bridge elsewhere. The coordinator therefore uses current availability and measured link health instead of forcing every message through every radio.

## Safety / RF behavior

- No sidecar carries plaintext; the existing encrypted Meshtastic MeshPacket is reused.
- Duplicate suppression and packet IDs remain Meshtastic-native.
- ESP-NOW callbacks only hand work to queues; routing decisions run in normal tasks.
- XBee/SX1262 coexistence remains guarded by the existing RF coexistence scheduler.
- All state is bounded; no unbounded queues are introduced.
