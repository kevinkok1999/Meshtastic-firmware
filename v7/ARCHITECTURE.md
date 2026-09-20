# V7 Architecture

## 1. System shape

Keep Saitama's existing layers and insert a transport coordinator beside the MeshService boundary rather than replacing MeshCore:

UI / App Logic
  -> HybridTransportManager
     -> MeshCore/LoRa path (existing, canonical interoperability)
     -> EspNowLRTransport (new, V7 direct path)
     -> future ITransport implementations (reserved only; no speculative code required)

Existing Saitama functions, contacts, channels, GPS, maps, BLE companion and SX1262 behavior must continue to work when V7 is set to LORA_ONLY.

## 2. Hardware isolation

The T-Deck Plus has separate radio paths:
- SX1262 for sub-GHz LoRa/MeshCore.
- ESP32-S3 2.4 GHz Wi-Fi radio for ESP-NOW LR.
BLE and Wi-Fi share ESP32-S3 2.4 GHz resources, so coexistence must be treated as a measured hardware constraint, not assumed perfect.

The upstream main.cpp currently executes WiFi.mode(WIFI_OFF). V7 must replace that unconditional shutdown with a small lifecycle manager:
- keep Wi-Fi off in LORA_ONLY and when ESP-NOW is disabled;
- initialize station mode + ESP-NOW only for modes that require it;
- enable WIFI_PROTOCOL_LR on the station interface;
- deinitialize cleanly when switching back to LoRa-only if safe.

Do not change the proven Saitama initialization ordering for BLE/LVGL/FSPI/SX1262 without a demonstrated need.

## 3. Transport contract

Define a Saitama-owned transport abstraction. Suggested API concepts:
- begin()/end()/tick()
- capability()
- canReach(peer)
- send(peer, encryptedPayload, metadata)
- receive queue/callback
- health(peer)
- stats()

HybridTransportManager owns route policy. UI code must not know ESP-NOW MAC details.

## 4. Route modes

AUTO:
1. For a V7 peer with a fresh ESP-NOW capability mapping and healthy direct path, try ESP-NOW.
2. Require application-level success/ACK semantics; the ESP-NOW send callback alone is not end-to-end delivery proof.
3. If unavailable/timeout/retry budget exhausted, fall back to existing MeshCore/LoRa.
4. Surface final route in diagnostics, not as a second message.

LORA_ONLY:
- Do not initialize ESP-NOW.
- Preserve Saitama v1.3.0 MeshCore behavior.

ESPNOW_LR_ONLY:
- Never silently use LoRa.
- Useful for deterministic hardware/range testing.

REDUNDANT:
- May transmit over both eligible transports for explicitly selected reliability use.
- A stable message ID and dedup cache are mandatory.

## 5. Identity and discovery

ESP-NOW addresses peers by Wi-Fi MAC; Saitama/MeshCore identifies contacts cryptographically. V7 therefore needs a capability-binding layer:
- locally derive a compact node/contact identifier from the existing MeshCore public identity;
- advertise only the minimum capability metadata needed to associate a V7 peer with a MAC;
- validate/bind a discovered MAC to the expected MeshCore identity before trusting it for private delivery;
- expire stale mappings;
- make discovery advertising configurable;
- do not expose private-message contents in discovery frames.

Discovery is a convenience/control plane. It must not replace MeshCore's cryptographic identity.

## 6. Data framing

MeshCore MAX_TRANS_UNIT is 255 bytes at the pinned revision. ESP-NOW compatibility must not depend on every peer supporting the largest newer ESP-NOW payload format. Use a versioned V7 envelope with conservative fragments.

Envelope fields should include:
- magic + protocol version
- frame type (data, ack, hello/capability, probe)
- message ID / sequence
- source/destination identity discriminator
- fragment index and fragment count
- payload length
- integrity check for framing

Keep buffers statically bounded. Reassembly must have:
- maximum message size,
- maximum concurrent assemblies,
- timeout,
- duplicate-fragment handling,
- malformed-frame rejection,
- no unbounded heap allocation.

## 7. Encryption rule

Do not implement a shortcut where a Saitama DM is decrypted to application text and then broadcast unencrypted over ESP-NOW.

Preferred implementation order:
A. Identify a clean point where an already protected MeshCore/private-message representation can be serialized and re-injected on receive.
B. If MeshCore's pinned API cannot safely expose that, build an explicit V7 secure payload layer using the same contact identity/key material only after a security review and test vectors.
C. Optional ESP-NOW PMK/LMK is defense in depth, not a substitute for end-to-end message protection.

Channel/public messages may have different MeshCore semantics; document them separately and never claim stronger privacy than the underlying protocol provides.

## 8. ACK, retry and dedup

Use an application-level message ID unique enough across reboot/session boundaries. Maintain bounded recent-ID caches. Route manager records:
- attempts by transport,
- ACK/timeout result,
- fallback count,
- duplicate drops,
- fragment retransmissions.

Retry with bounded backoff. Never create retry storms.

## 9. Config migration

Append V7 fields at the end of Saitama Config to preserve its existing NVS migration model. Candidate fields:
- transportMode
- espNowEnabled/discoveryEnabled
- espNowChannel or auto-channel policy
- espNowRetryLimit
- espNowAckTimeoutMs
- redundantCriticalOnly (if REDUNDANT is kept)

Defaults after upgrading from Saitama 1.3.0 must be conservative: AUTO may be selected, but absence/failure of ESP-NOW must transparently leave normal LoRa functional.

## 10. UI and diagnostics

Settings:
- Transport: Auto / LoRa / ESP-NOW LR / Redundant
- ESP-NOW discovery toggle
- diagnostic channel/control settings only where needed

Chat/contacts:
- optional compact route indicator after successful send
- V7 capability indication for peers
- never block a LoRa contact just because it lacks V7 capability

Diagnostics:
- ESP-NOW initialized/channel/LR state
- known V7 peers and mapping age
- TX/RX/ACK/retry/fallback/dedup/reassembly counters
- last route and error
- LoRa stats remain visible

## 11. Power and coexistence

LORA_ONLY must preserve the upstream WiFi-off power behavior.
AUTO should avoid keeping unnecessary active scanning/transmission running continuously.
Test BLE disabled, BLE advertising, and BLE connected states against ESP-NOW TX/RX. Record failure rates and latency.

## 12. Extensibility

Do not implement XBee in this V7 task. Design the interface so a future XBeeTransport can be added without changing chat/UI business logic. This is an architectural seam, not scope expansion.
