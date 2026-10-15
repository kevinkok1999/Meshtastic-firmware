# XR Privacy Architecture

## Product objective

XR must remain useful when the internet is unavailable while also being safe to use when an internet path is available. LoRa/mesh remains the primary infrastructure-independent transport. Wi-Fi/MQTT is an opportunistic transport, not a trusted party.

The privacy goal is not merely encrypted radio. It is a layered policy covering message content, identity, metadata, storage, public Wi-Fi, MQTT, Courier and recovery.

## Hard privacy rules

1. Private direct-message content is destination protected end-to-end.
2. Transport changes must never downgrade message protection.
3. A public Wi-Fi access point is always treated as hostile/untrusted infrastructure.
4. Internet transport is allowed only after server/broker identity verification.
5. `setInsecure()` is not acceptable for XR production internet delivery.
6. Courier nodes carry opaque destination-protected data and must not receive plaintext third-party messages.
7. Private payloads written to persistent storage require the protected storage path.
8. Optional position, telemetry and diagnostics are not attached to private messages unless explicitly requested.
9. Raw SSIDs are not retained as XR learning history; the learning engine stores a local fingerprint instead.
10. A privacy failure fails closed: keep the message queued for another permitted transport.

## Threat model

XR assumes the following can be curious, malicious or compromised:

- public Wi-Fi operators;
- other clients on a public hotspot;
- malicious look-alike access points;
- an MQTT broker or network observer;
- unrelated mesh relays;
- third-party Courier devices;
- passive RF observers;
- a lost/stolen device while powered off.

No design can fully hide radio presence, packet timing or all routing metadata from a sufficiently capable observer. XR must say this clearly and minimize exposure rather than claiming anonymity.

## Layer 1 — Message content

### Private direct messages

Use the existing Meshtastic PKI path and authoritative peer keys. XR does not invent a replacement cipher. A private DM may use LoRa, MQTT, local Wi-Fi, BLE proximity or Courier, but the destination-protected payload remains the same trust boundary.

If an authoritative peer key is unavailable, XR must not silently label a message private. The UI can wait for key verification/discovery or use the existing compatible behavior with an explicit privacy state.

### Public/group channels

Preserve original Meshtastic channel behavior. XR must not claim one-to-one privacy for group-channel messages simply because the transport itself is encrypted.

## Layer 2 — Transport privacy

### LoRa/mesh

Relays forward normal compatible Meshtastic traffic. Private payload confidentiality remains end-to-end. Relay selection must not require plaintext.

### Public Wi-Fi

Public Wi-Fi is a bearer only. XR must:

- scan asynchronously;
- avoid blocking the message/radio loop;
- never trust the hotspot for content security;
- detect whether internet transport is actually usable;
- reject captive portals that require new human interaction instead of bypassing them;
- reuse previously authorized access where technically and contractually valid;
- require authenticated application transport before declaring the path usable.

### MQTT

TLS encryption without broker authentication is insufficient. Production XR requires broker identity verification (trusted CA/certificate bundle or an equivalent maintained trust-anchor strategy). If broker verification fails, internet delivery is unavailable and the message remains queued for LoRa/Courier/retry.

## Layer 3 — Metadata privacy

### Local learning

The Network Autopilot stores a one-way/local network key rather than raw SSID history. Diagnostic export must be opt-in and should redact BSSID, SSID and precise timing unless the user explicitly requests raw diagnostics.

### Per-network Wi-Fi pseudonym

Research/production target: derive a stable locally-administered station MAC per approved network from a device secret plus the local network fingerprint. This can reduce cross-network correlation while remaining stable enough for a hotspot that expects the same client during later reconnects.

Do not rotate MAC during an active session. Do not use this on a network where its access policy requires the hardware MAC.

### MQTT metadata

Standard Meshtastic MQTT may expose routing/topic/gateway metadata to the broker even when payload content is encrypted. This is an explicit limitation.

For XR-to-XR high-privacy internet fallback, research a separate optional **XR Blind Relay** mode:

- end-to-end encrypted opaque envelope;
- rotating recipient rendezvous tokens;
- no plaintext contacts/message text at relay;
- short retention;
- bounded queue;
- anti-abuse/rate limits;
- no precise position metadata;
- cryptographic delivery receipt;
- compatibility fallback to standard Meshtastic MQTT when Blind Relay is unavailable.

Blind Relay is an enhancement path, not a replacement for standard Meshtastic interoperability.

## Layer 4 — Courier privacy

Courier custody must remain blind:

- courier stores ciphertext/opaque payload only;
- stable application message ID is not required to be a global public identifier;
- custody handoff uses bounded metadata;
- receipt contains the minimum necessary delivery proof;
- expired/cancelled records are deleted through the storage journal;
- one courier cannot query arbitrary message history.

## Layer 5 — Storage privacy

Core no-SD operation needs:

- crash-safe journal;
- integrity checking;
- quota separation;
- encrypted/protected storage for private message state;
- no plaintext message body in debug logs;
- bounded retention;
- explicit local-history setting;
- secure deletion semantics where realistically possible on flash (key destruction preferred over claims of physical-byte erasure).

## Layer 6 — Privacy-aware Hybrid Delivery

For every outgoing private DM:

1. Validate peer identity/privacy requirements.
2. Create/retain the normal Meshtastic destination-protected message.
3. Prefer LoRa/mesh for infrastructure independence.
4. If delivery is pending, evaluate approved/public network candidates.
5. Verify internet path.
6. Verify authenticated broker/relay identity.
7. Offer the same logical message through the internet transport without creating a second chat entry.
8. Deduplicate by logical message identity/receipt.
9. Mark `Delivered` only after destination-level confirmation, not merely Wi-Fi association or broker publish.
10. If privacy checks fail, retain `Waiting for route` and continue LoRa/Courier recovery.

## Privacy modes

### Standard
Original Meshtastic-compatible behavior plus XR transport safety.

### Private
Private DMs require authoritative destination identity and authenticated internet transport. Optional metadata suppressed.

### High Privacy
Adds stricter metadata minimization, per-network Wi-Fi pseudonym where supported, shorter local retention, no automatic location metadata and XR Blind Relay preference once implemented.

No mode may bypass regional RF rules or disable the original Meshtastic security mechanisms.

## Release gates

A release cannot be called privacy-ready until all are true:

- no production XR internet path uses `setInsecure()`;
- private-DM tests fail closed when broker identity is not verified;
- LoRa -> Wi-Fi -> LoRa transport changes keep the same logical chat/message identity;
- relay/Courier tests prove plaintext is unavailable to intermediaries;
- no private message content appears in normal logs;
- reboot/crash recovery does not expose plaintext journal fragments;
- duplicate delivery remains suppressed across transports;
- public Wi-Fi failure does not destroy the queued message;
- a fake hotspot cannot cause a message to be marked delivered;
- location/telemetry remains opt-in for private messages;
- privacy states are visible in message details without cluttering the normal chat experience.

## Priority relative to range

The product North Star remains direct T-Deck-to-T-Deck usable range, with 5 km as the engineering target under suitable field conditions. Privacy is a co-equal release gate: range improvements that require weakening private-message protection are rejected.
