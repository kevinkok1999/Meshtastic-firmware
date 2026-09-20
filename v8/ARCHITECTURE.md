# V8 Architecture

## 1. System shape

Keep the Saitama/MeshCore application and existing LoRa path intact. Replace the V7 two-path decision with a coordinator around the transport boundary:

UI / Chat
  -> V8 Message Coordinator
     -> Existing MeshCore/SX1262 LoRa path
     -> ESP-NOW LR transport
     -> XBee XR 868 transport
     -> LR2021 transport
  -> Delivery state / diagnostics

The coordinator owns route policy. Screens must not know MAC addresses, XBee 64-bit addresses, SPI pins or RF scheduling details.

## 2. Backward compatibility

V8 must still communicate with:
- normal Saitama/MeshCore nodes over LoRa;
- V7 nodes over the existing V7 ESP-NOW protocol where possible;
- V8 nodes over the new capability/envelope layer.

V8 must preserve the V7 ESP-NOW M7/protocol-v1 parser for V7 peers. V8-only capabilities are negotiated separately and must not make a V7 peer unusable.

## 3. Transport contract

Each optional transport implements the same logical contract:

- begin(profile)
- end()
- poll(now)
- available()
- canReach(peer)
- mtu()
- send(frame)
- metrics(peer)
- receive sink
- diagnostics()

Transport implementations do not own chat history, UI, global retries or cross-transport fallback.

Transport types:
- LORA_MESHCORE
- ESPNOW_LR
- XBEE_XR868
- LR2021_GEN4

## 4. Shared message envelope

V8 replaces text-hash duplicate detection with a stable transport-independent message identity.

Every V8 alternate-bearer message carries:
- magic/version
- frame type
- flags
- 64-bit message ID
- source identity prefix
- destination identity prefix
- fragment index/count
- payload length
- protected payload bytes

Message ID generation must survive repeated identical user text. Suggested construction: boot/session nonce + monotonic counter, with collision tests.

Encrypt/authenticate before fragmentation. Relays forward ciphertext and metadata; they do not need plaintext.

Use statically bounded buffers and bounded reassembly slots. No unbounded heap growth.

## 5. Security

MeshCore public identity remains the node identity.

V8 capability advertisements bind:
- MeshCore public identity
- ESP-NOW MAC when present
- XBee 64-bit address when present
- LR2021 capability/profile
- protocol version/capability bits

The binding must be signed/verified before a transport address becomes trusted for private delivery.

The V7 identity-derived ECDH + authenticated encryption concept remains the baseline for alternate direct transports. Native XBee/ESP-NOW encryption may be used as defense in depth, not as a replacement for end-to-end protection.

## 6. Route manager

Modes:
- AUTO_BALANCED
- RANGE_FIRST
- POWER_SAVE
- LORA_ONLY
- ESPNOW_ONLY
- XBEE_ONLY
- LR2021_ONLY
- REDUNDANT

Each link exposes normalized metrics:
- available
- peerReachable
- delivery probability EWMA
- recent ACK latency
- RSSI/SNR where meaningful
- queue depth
- MTU
- energy cost
- metric age
- recent failure streak

Do not compare raw RSSI across different PHYs as if they are identical.

AUTO_BALANCED:
- reliability dominates;
- then latency, congestion and energy refine the score;
- sticky hysteresis prevents constant route flapping.

RANGE_FIRST:
- prioritizes measured successful-delivery history and link-margin class;
- permits slower links if they have a better observed path;
- never assumes XBee is automatically longer-range than LoRa.

POWER_SAVE:
- heavily weights energy cost while maintaining a delivery floor.

REDUNDANT:
- may send selected messages on two independent eligible bearers;
- receiver delivers only once using the 64-bit message ID.

## 7. Sub-GHz RF arbiter

SX1262, XBee XR 868 and LR2021 may all operate around EU868.

Introduce a single SubGhzRfArbiter:
- one local sub-GHz transmitter at a time;
- optional receive holdoff/guard time around a nearby transmission;
- records blocked/queued airtime;
- exposes coexistence metrics;
- never bypasses regional duty-cycle/listen-before-talk requirements.

ESP-NOW LR uses the ESP32-S3 2.4 GHz radio and does not use this arbiter, though BLE/Wi-Fi coexistence still needs testing.

## 8. XBee XR 868 adapter

Reuse concepts from feature/xbee-xr868-transport, but port into the Saitama V8 overlay rather than merging the Meshtastic-oriented branch.

Requirements:
- API mode, non-blocking parser;
- runtime-configured UART pins;
- module identity/capability query before declaring ready;
- runtime NP/MTU discovery;
- 64-bit address mapping bound to MeshCore identity;
- TX-status feedback into delivery metrics;
- no hard-coded GPS UART pins.

## 9. LR2021 adapter

Saitama v1.3.0 already depends on RadioLib ^7.6.0, and LR2021 support exists in that generation.

V8 should pin an exact validated RadioLib version during coding instead of leaving the dependency floating.

Initial LR2021 mode:
- EU868-compatible LoRa PHY;
- transport adapter behind the same coordinator;
- external SPI hardware only;
- capability disabled if the device is absent.

Before claiming interoperability with onboard SX1262 peers, run a byte-for-byte packet compatibility test with the exact MeshCore modem settings.

Advanced LR2021 features such as FLRC/LR-FHSS/dual-band remain opt-in experiments until normal V8 messaging passes regression tests.

## 10. Queueing and delivery

V7 permits one pending ESP-NOW send. V8 replaces that with a small bounded delivery queue.

Per item track:
- message ID
- destination
- selected transport(s)
- attempt count
- send timestamp
- ACK state
- fallback state

No retry storm:
- bounded attempts
- bounded backoff
- one fallback transition at a time
- queue-pressure counters
- explicit drop reason

Application-level ACK remains the final delivery signal. Link-layer callbacks/status only update link health.

## 11. Store-and-forward

Design the interface now, but do not make cross-protocol relaying default in the first coding milestone.

Future gateway mode may forward an already protected V8 envelope from one bearer to another. It must enforce:
- TTL/hop budget
- dedupe by message ID
- no plaintext relay requirement
- bounded queue
- loop prevention

## 12. UI

Chat remains unchanged for normal use.

Optional compact delivery label:
- LoRa
- ESP-NOW LR
- XBee
- LR2021
- Redundant

Diagnostics page:
- hardware detected
- peer capability
- route score/reason
- delivery EWMA
- latency
- retries/fallbacks
- queue depth
- RF-arbiter waits
- last error

The device must remain usable if every V8-specific diagnostics feature is disabled.
