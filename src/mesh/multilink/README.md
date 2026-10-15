# Experimental Adaptive Multi-Link Transport

This directory contains an experimental transport-selection layer for Meshtastic. The initial target is the LilyGo T-Deck / T-Deck Plus class of ESP32-S3 devices.

The goal is not to replace LoRa. The goal is to keep Meshtastic's existing packet/security model while allowing an already prepared packet to use the most appropriate available bearer.

## Status

### Implemented

- Transport-neutral `LinkTransport` API.
- Adaptive scoring using availability, destination support, delivery EWMA, RSSI, latency, congestion, energy cost and traffic class.
- Sticky hysteresis to avoid rapid bearer flapping.
- Optional duplicate transmission of control traffic over two independent links.
- `(from, messageId)` receive deduplication helper.
- ESP-NOW transport abstraction.
- ESP32 ESP-NOW backend with known-peer routing, receive buffering, delivery callbacks, RSSI tracking and optional PMK/LMK transport encryption.
- Backscatter transport abstraction with `Disabled`, `ExternalTag`, `ReaderAssisted` and `Ambient` modes.
- T-Deck opt-in build flag: `MESHTASTIC_EXPERIMENTAL_MULTILINK=1`.
- Native/unit-testable policy, ESP-NOW and backscatter behavior.

### Not yet wired into the production packet path

The experimental code is intentionally not inserted into `Router`/`RadioInterface` yet. The integration point must preserve Meshtastic's existing encryption, retransmission, ownership and packet-history rules. Until that bridge is complete, enabling the build flag does not change normal LoRa routing behavior.

## Intended packet flow

```text
Meshtastic Router
      |
      | encoded/encrypted wire packet
      v
MultiLink Bridge
      |
      +---- AdaptiveLinkManager ----> ESP-NOW
      |                         \----> LoRa adapter
      |                          \---> Wi-Fi Aware/NAN (planned)
      |                           \--> BLE peer bearer (planned)
      |                            \-> Backscatter front-end
      |
      +<--- unified RX + dedupe <----- all enabled bearers
      |
      v
Meshtastic Router
```

## Selection policy

The selector rejects a bearer if it is unavailable, cannot reach/support the destination, does not satisfy the required effective encryption policy, is below the delivery floor, has stale metrics, or cannot fit the frame.

The remaining bearers are scored primarily on measured delivery probability. RSSI, latency, queue depth, relative energy cost and traffic class refine the choice. A sticky window plus hysteresis keeps a slightly fluctuating signal from switching bearers on every packet.

Current traffic hints:

- `Control`: prioritize reliability; optional two-bearer duplication.
- `Interactive`: prefer low-latency local links such as ESP-NOW/NAN when healthy.
- `Telemetry`: permit very-low-power backscatter when the hardware reports a usable carrier/front-end.
- `Bulk`: prefer high-throughput local links; the initial backscatter implementation rejects bulk traffic.

These are policy hints, not permanent constants. They must be tuned using real T-Deck measurements.

## Security model

`LinkMetrics::encrypted` means the frame receives the effective protection required by the upper layer; it does not mean every bearer necessarily supplies native link encryption.

Meshtastic's existing encrypted/authenticated packet format should remain the primary security boundary. Native bearer protection (for example ESP-NOW PMK/LMK) is defense in depth.

No default ESP-NOW PMK or LMK is hardcoded. Encrypted ESP-NOW peers must be provisioned with keys.

Wi-Fi Aware/NAN must not be treated as secure merely because it is Wi-Fi. ESP-IDF NAN capabilities and security APIs differ between SDK generations, so the future NAN backend must advertise only the security properties actually enabled on that build.

## ESP-NOW backend

The ESP32 backend:

- preserves an already-active Wi-Fi mode when possible;
- starts STA mode only when Wi-Fi is not active;
- maps Meshtastic node IDs to known peer MAC addresses;
- can register encrypted peers with a caller-provided LMK after a PMK is configured;
- limits payloads to an interoperable 240-byte budget;
- keeps vendor callbacks short by copying delivery/RX events into small queues;
- dispatches received frames from normal `poll()` context rather than directly from the Wi-Fi callback;
- allows one outstanding send at a time until the send callback completes.

Peer discovery/provisioning is deliberately separate from the transport driver and remains to be implemented.

## Backscatter is a real hardware transport, not a software label

`BackscatterLink` is a first-class bearer in the selector. A concrete `BackscatterFrontEnd` must report whether its RF hardware is available, whether a usable carrier/reader signal is present, estimated signal strength, and whether it accepted a modulation request.

The public T-Deck documentation does not establish that the stock SX1262 antenna path exposes a software-controlled load suitable for ambient backscatter. Therefore the current contract assumes an external RF switch/tag/front-end until hardware measurements and schematics prove an onboard path.

Planned front-end work:

1. Select and document a safe low-voltage external load-modulation front-end.
2. Implement precise symbol generation (prefer hardware timer/RMT/DMA over long busy loops).
3. Add carrier qualification and calibration.
4. Add a compact framed waveform with preamble, length, sequence number and integrity check.
5. Add a receiver/reader path or reader-assisted gateway implementation.
6. Measure range, packet error rate, energy and coexistence before raising MTU/rate or permitting bulk traffic.

Until those measurements exist, backscatter defaults should remain conservative and experimental.

## Planned NAN and BLE work

Wi-Fi Aware/NAN is attractive for discovery and a local direct datapath, but its lifecycle is different from normal STA/AP Wi-Fi and APIs change across ESP-IDF versions. Implement it behind a backend interface with compile-time capability checks rather than placing NAN calls inside the selector.

BLE peer relaying must coexist with Meshtastic's existing phone/client BLE functionality. It should therefore share no callbacks or connection state until the current Bluetooth service ownership is mapped and tested.

## Production integration checklist

Before this can become a flashable production feature:

- Add a bridge at the encrypted/wire-packet boundary, not the decoded application payload boundary.
- Add a LoRa adapter so LoRa participates in the same scoring system without changing existing legal duty-cycle enforcement.
- Feed received alternate-bearer packets through existing packet history, authentication and router handling.
- Define node-ID/MAC discovery and authenticated peer enrollment.
- Persist transport settings and keys without logging secrets.
- Add queue backpressure, watchdog metrics and failure counters.
- Validate ESP-NOW channel coexistence with Wi-Fi/BLE on T-Deck.
- Add SDK-version CI/build jobs for the T-Deck target.
- Bench-test two real T-Deck devices before making adaptive routing the default.
- Keep backscatter behind a separate experimental capability until an actual front-end passes RF and reliability testing.

## Current validation

The transport-neutral C++ core and ESP-NOW link flow have been syntax/compile tested with C++17 and strict warnings in a host-side harness. Unity test sources are included for adaptive selection, ESP-NOW behavior and backscatter gating.

The ESP32 ESP-NOW backend has been written against the current ESP-IDF callback/API model, but still requires a real PlatformIO T-Deck build and hardware test before it should be called production-ready.
