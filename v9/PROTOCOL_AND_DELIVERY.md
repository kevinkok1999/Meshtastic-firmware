# V9 Protocol and Delivery Contract

## 1. V9 message identity

Use a 128-bit V9 MessageId for V9-native envelopes.

Construction requirement:
- node/session component;
- persistent or collision-resistant session nonce;
- monotonic per-session counter.

Goals:
- repeated identical text always has a different MessageId;
- reboot collisions are impractical;
- dedupe can survive multipath and delayed forwarding.

V8/V7 compatibility adapters keep their native identifiers and map them into local V9 delivery state without rewriting historical wire formats.

## 2. Envelope

V9 native envelope fields:
- magic + protocol version;
- frame type;
- flags;
- MessageId;
- source identity short reference;
- destination identity short reference;
- creation time class;
- expiry/TTL;
- hop budget;
- fragment index/count;
- payload length;
- protected payload;
- authentication data.

Keep headers compact. Optional fields use capability-negotiated extensions.

## 3. Security boundary

Encrypt/authenticate application payload before:
- fragmentation;
- FEC;
- store-forward;
- cross-bearer relay.

Relays do not need private-message plaintext.

Signed capability records bind:
- MeshCore identity;
- V9 protocol version;
- ESP-NOW address/capability;
- XBee address/capability;
- LR2021 capability/profile;
- relay/store-forward capability.

Add:
- replay window;
- per-peer rate limits;
- invalid-auth counters;
- signed capability expiry.

## 4. Fragmentation

Fragment size is selected per path.

Inputs:
- path MTU;
- recent fragment loss;
- queue pressure;
- expected airtime;
- selected recovery policy.

Rules:
- small weak-link fragments reduce expensive whole-message loss;
- strong links may use larger fragments;
- all buffers are bounded;
- maximum fragments per message is fixed at compile time.

## 5. Selective-repeat ARQ

ACK frame carries a bounded fragment bitmap.

Sender behavior:
- send a window of fragments;
- receiver ACKs received/missing bitmap;
- resend only missing fragments;
- bounded retry rounds;
- exponential or table-based bounded backoff;
- route fallback may occur between rounds.

Never resend the full message merely because one fragment was missed.

## 6. FEC

FEC is optional and policy-driven.

First implementation:
- systematic erasure recovery;
- fixed-size small block;
- one parity fragment per block;
- can recover one missing fragment in a protected block;
- fixed buffers only;
- host-testable without radio hardware.

Why start here:
- predictable CPU/RAM;
- no heavy dependency;
- useful on marginal links;
- easy to disable when it increases airtime more than it saves.

The codec interface must allow a stronger future Reed-Solomon implementation without changing the delivery coordinator.

FEC enable rules consider:
- recent loss;
- message size;
- link confidence;
- airtime budget.

## 7. Store-and-forward

Store only protected V9 envelopes.

Queue properties:
- bounded item count;
- bounded total bytes;
- per-message expiry;
- priority;
- retry-after time;
- next-hop candidates;
- delivery-attempt count.

Persistent storage:
- append/journal style;
- atomic record validation;
- CRC/integrity marker;
- compaction threshold;
- flash-write rate limiting;
- hard byte quota.

A filesystem failure disables persistent queueing rather than messaging.

## 8. Relay rules

A V9 relay may forward a protected envelope only when:
- authentication/format checks pass;
- TTL is valid;
- hop budget remains;
- MessageId is not in dedupe cache;
- queue capacity exists;
- rate limit permits.

Loop prevention:
- MessageId dedupe;
- hop budget;
- short-lived next-hop history.

## 9. Priorities

Priority classes:
- NORMAL
- IMPORTANT
- CONTROL

CONTROL is reserved for protocol operation and cannot be user-spammed.

IMPORTANT may:
- receive larger retry budget;
- use redundant paths;
- use store-forward earlier.

Priority does not bypass RF/regulatory constraints.

## 10. Delivery states

UI-facing states:
- queued;
- sending;
- delivered;
- stored for later delivery;
- expired;
- failed.

Technical reason is retained for diagnostics:
- no route;
- retry exhausted;
- expired;
- queue full;
- auth failure;
- transport unavailable;
- RF arbiter timeout.
