# V9 Architecture

## 1. Layering

UI / Chat
  -> V9 Delivery Coordinator
     -> V9 Reach Engine
        -> Path Manager
        -> Link Metric Store
        -> Fragment/ACK Engine
        -> FEC Policy
        -> Store-and-Forward Queue
        -> Sub-GHz RF Arbiter
     -> Transport adapters
        -> MeshCore / onboard SX1262
        -> ESP-NOW LR
        -> XBee XR 868
        -> LR2021
  -> Delivery state / diagnostics

Screens never choose MAC addresses, channels, SPI pins or XBee addresses.

## 2. Compatibility ladder

V9 peer:
- V9 envelope, fragment ACKs, multipath, optional FEC and store-forward.

V8 peer:
- fall back to the V8 transport/envelope capability set;
- do not send V9-only frames without capability negotiation.

V7 peer:
- preserve V7 ESP-NOW direct compatibility where already supported;
- otherwise normal MeshCore/LoRa.

Legacy MeshCore peer:
- normal LoRa messaging only.

Compatibility failure must reduce features, not break chat.

## 3. Reach Engine responsibilities

The Reach Engine owns:
- bearer eligibility;
- normalized link quality;
- route/path scoring;
- path hysteresis;
- primary + backup path selection;
- fragment size;
- retry budget;
- FEC enable/disable;
- store-forward decision;
- expiry and delivery state.

It does not own:
- message plaintext;
- contact UI;
- radio-specific register details;
- persistent chat history.

## 4. Link metrics

Each peer/bearer exposes:
- availability;
- reachability confidence;
- delivery EWMA;
- ACK latency EWMA;
- retry EWMA;
- RSSI and SNR where meaningful;
- normalized link-margin class;
- queue depth;
- estimated airtime per payload byte;
- energy cost class;
- metric age;
- consecutive failure count;
- RF-busy percentage where measurable.

Raw RSSI from unlike PHYs is never compared directly.

## 5. Normalized link confidence

Every transport maps its native measurements into:
- UNKNOWN
- POOR
- MARGINAL
- GOOD
- STRONG

The mapping is transport-specific and host-testable.

Path scoring uses this normalized class plus measured delivery history.

## 6. Route modes

AUTO:
- reliability first;
- low route churn;
- energy and latency secondary.

RANGE_FIRST:
- prefers best measured end-to-end delivery probability;
- tolerates slower links;
- enables stronger recovery behavior sooner.

POWER_SAVE:
- chooses lower energy cost while enforcing a minimum delivery confidence.

LOW_LATENCY:
- prioritizes direct fast links when reliability is adequate.

REDUNDANT:
- may use two independent paths for priority messages.

LORA_ONLY / ESPNOW_ONLY / XBEE_ONLY / LR2021_ONLY:
- diagnostic/manual modes;
- unavailable requested hardware produces a clear error, never a silent fake route.

## 7. Multipath

Maintain at most:
- one active primary path;
- one warm backup path.

A backup path should be as failure-independent from the primary as practical.

Examples:
- ESP-NOW direct primary + LoRa mesh backup;
- LR2021 direct primary + XBee/DigiMesh backup;
- LoRa path via relay A + alternate V9 relay path via relay B.

Do not broadcast every message over every bearer.

## 8. Path cost

Path cost is a weighted combination of:
- expected transmissions;
- airtime;
- congestion;
- latency;
- energy;
- staleness;
- recent failures.

For a multi-hop route:
- weak-hop penalties accumulate;
- a single very unreliable hop can invalidate the route;
- three strong hops may outrank one unstable direct hop.

The exact weights are configuration constants covered by deterministic tests.

## 9. Sub-GHz coexistence

Onboard SX1262, XBee XR 868 and LR2021 can all occupy the EU868 environment.

One local SubGhzRfArbiter owns transmission grants.

Requirements:
- serialize local sub-GHz TX;
- bounded queue;
- priority classes;
- guard interval support;
- measured blocked-airtime counter;
- no starvation;
- no bypass of regional radio constraints.

ESP-NOW LR remains outside this arbiter because it is 2.4 GHz, but Wi-Fi/BLE coexistence remains testable.

## 10. Failure domains

No single optional subsystem may freeze chat.

Failures are isolated:
- XBee unplugged -> XBee unavailable;
- LR2021 init fail -> LR2021 unavailable;
- store-forward filesystem error -> RAM-only/no-store mode;
- route metric corruption -> conservative LoRa fallback;
- FEC codec error -> retransmission without FEC;
- diagnostics failure -> messaging continues.

## 11. No unsafe assumptions

V9 must not:
- assume XBee is always longer-range than LoRa;
- assume LR2021 is automatically compatible with every SX1262 packet profile;
- invent expansion GPIOs;
- share antennas without designed RF hardware;
- increase RF output beyond validated regional/module profiles.
