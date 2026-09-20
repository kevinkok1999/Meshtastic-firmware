# V10 DirectLink Protocol

## 1. Pair identity

V10 reuses the existing cryptographic identity as the trust root.

A PairSession stores:
- local public identity;
- remote public identity;
- remote ESP MAC;
- negotiated V10 capability bitmap;
- pair epoch;
- session nonce;
- per-session message counter.

No third node participates.

## 2. Capability bitmap

Capability bits:
- V10_DIRECTLINK
- LORA_COMPAT
- GFSK_DIRECT
- ESPNOW_LR
- WIFI_LR_DIRECT
- BLE_CODED
- SELECTIVE_ACK
- FEC_XOR1
- ADAPTIVE_FRAGMENT

Capability exchange is authenticated.

Unsupported bearer => simply unavailable.

## 3. Message identity

Use V10 MessageId128.

Every user send receives one ID before a bearer is selected.

The same MessageId follows the message across:
- retry;
- different bearer;
- fragmentation;
- fallback.

This guarantees cross-bearer dedupe without hashing the plaintext.

## 4. Frame classes

HELLO
PAIR_CAPS
PROBE
PROBE_ACK
DATA
SELECTIVE_ACK
DELIVERY_ACK
CONTROL

CONTROL is bounded and not user-generated.

## 5. Protected payload

Application payload is authenticated/encrypted before transport-specific wrapping.

Bearers transport opaque V10 data.

This means:
- LoRa/GFSK/ESP-NOW/Wi-Fi/BLE use the same message identity;
- duplicate detection is bearer-independent;
- a bearer adapter does not need message plaintext.

## 6. Fragmentation

Fragment profile is selected by DirectLinkBrain.

Profiles:
- XL
- LARGE
- MEDIUM
- SMALL
- TINY

Actual sizes are derived from the smallest MTU of the selected bearer/profile.

Weak links favor smaller fragments.
Strong links favor lower overhead.

## 7. Selective ACK

Receiver maintains a bounded bitmap.

Sender only retransmits missing fragments.

ACK can arrive through:
- same bearer;
- an alternate already-active bearer when explicitly permitted.

Cross-bearer ACK uses the same MessageId and authenticated session.

## 8. FEC

V10.0 starts with the already host-tested bounded XOR single-erasure codec.

Policy:
- off on strong links;
- optional on marginal/high-loss links;
- never enabled when parity airtime costs more than the estimated retransmission saving.

Future stronger FEC stays behind the same interface.

## 9. Probe frames

Probes are:
- short;
- authenticated;
- rate limited;
- lower priority than an actual queued message.

A probe measures availability and ACK timing.
It is not a continuous beacon flood.

## 10. Delivery completion

User-visible state:
- Sending
- Delivered
- Failed

Internally retain:
- primary bearer;
- final bearer;
- fragment retries;
- fallback count;
- delivery latency;
- failure reason.

No mesh/store-forward state is required for the V10 A<->B primary mode.
