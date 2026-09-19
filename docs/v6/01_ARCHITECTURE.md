# V6 Architecture

## Objective
V6 is not "V5 plus more transports". It introduces a V6-native message core that sits above all transports.

## Layer model

UI
-> Conversation Core
-> Privacy/Crypto Engine
-> Secure Envelope
-> Delivery Engine
-> Routing/Policy Engine
-> Transport API
-> LoRa | ESP-NOW | XBee | Internet | Meshtastic Compatibility

### Rule: upper layers never call radio-specific code directly.
A conversation creates a logical message. The message is encrypted once into a secure object. Delivery and transports move that object without needing plaintext.

## Firmware language
Keep the device firmware in modern C++.

Reasons:
- proven T-Deck display, keyboard, SX1262, ESP-NOW, XBee, Wi-Fi and Meshtastic code already exists;
- ESP-IDF/Arduino/FreeRTOS ecosystem is native C/C++;
- a rewrite would add hardware regression risk without improving the protocol itself.

Use Rust for the blind internet relay, not for an immediate firmware rewrite.

## Core components

### ConversationCore
Owns chats, groups, drafts, receipts and user-visible state.
Must not own radio logic.

### MessageCore
Creates immutable logical MessageId values and normalizes text/system/group messages.

### CryptoProvider
Only module allowed to convert plaintext <-> protected payload.
No transport receives plaintext.

### IdentityManager
Owns local identity, verified contacts and key-change state.

### SecureEnvelopeCodec
Encodes/decodes the transport-independent V6 wire object.
No UI logic.

### DeliveryEngine
Owns queued/sent/relayed/delivered/expired state.

### Router
Selects next logical hop/destination opportunities.

### TransportPolicy
Enforces user mode:
- OFF_GRID: LoRa/ESP-NOW/XBee/local relays only.
- INTERNET: Internet transport only.
- SMART: explicitly opted-in policy may use multiple permitted transports.

### Transport implementations
All implement one bounded interface:
- start()
- stop()
- available()
- mtu()
- send(envelope)
- pollReceive()
- metrics()
- supportsBroadcast()
- energyClass()

### MessageStore
Transactional encrypted local storage.
The rest of the system never writes raw chat files.

### EventBus
Typed internal events instead of cross-module mutation.

Examples:
MessageCreated
MessageProtected
EnvelopeQueued
PeerDiscovered
RouteUpdated
TransportAccepted
EnvelopeReceived
MessageDelivered
IdentityChanged

## Task ownership
Recommended FreeRTOS ownership:
- UI task
- message/core task
- crypto worker
- storage worker
- LoRa task
- ESP-NOW task
- XBee task
- network task

One owner per mutable subsystem. Cross-task communication uses bounded queues.

## Memory rules
- no unbounded queues;
- no unbounded skipped-key cache;
- avoid heap allocation in packet hot paths;
- fixed maximum fragment/reassembly slots;
- fixed maximum relay custody entries;
- hard limits included in protocol tests.

## Compatibility
V6 Native and Meshtastic Compatibility are separate adapters.
V6-native packets must not be forced to fit every Meshtastic semantic.
Meshtastic remains available where interoperability is desired.

## Build profiles

### v6-dev
- recoverable;
- verbose privacy-safe diagnostics;
- no irreversible eFuse operations;
- browser/full-flash development possible.

### v6-secure-production
- signed firmware;
- Secure Boot / Flash Encryption only after provisioning design is proven;
- minimal diagnostics;
- signed OTA/update path;
- irreversible configuration performed only by explicit manufacturing/provisioning procedure.

Never burn production eFuses from ordinary developer builds.
