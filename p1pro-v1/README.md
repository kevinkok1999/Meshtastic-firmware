# MeshOffGridNL P1 Pro V1 — Base Station

Status: CODING PHASE 1 / NOT FOR PRODUCTION YET

## Goal

Build a dedicated SenseCAP Solar / P1 Pro repeater image that remains compatible with the MeshCore LoRa route used by the proven T-Deck V19 line, while making the base station capable of long-path operation without turning 48 hops into the normal routing behaviour.

## Pinned upstream

- Project: meshcore-dev/MeshCore
- Commit: e94125987ed87497e706a0b54d1e80c709343980
- Target: SenseCap_Solar_repeater
- MCU: nRF52840
- Radio: SX1262
- P1/P1-Pro base target: SenseCAP Solar

The upstream commit is pinned. Upstream main is never built implicitly.

## V1 routing contract

Fresh-install defaults:

- maximum scoped flood path: 48 hops
- maximum unscoped flood path: 6 hops
- maximum advert flood path: 8 hops
- outgoing flood path hash mode: 1-byte (mode 0), required for paths longer than 32 hops
- flood advert interval: keep upstream 47-hour default
- no queue-size inflation in V1 phase 1
- no hidden TX-power increase
- no change to the proven T-Deck V19 firmware

Persisted configuration is bounded on boot:
- flood_max cannot exceed 48
- flood_max_unscoped cannot exceed 6
- flood_max_advert cannot exceed 8
- path_hash_mode is forced to 1-byte for the V1 base-station profile

Why: 48 hops is a ceiling, not a target. Known routes should still use the shortest available path.

## Reliability rules

1. The radio/routing dataplane always has priority over GPS, management and diagnostics.
2. Never use unbounded packet queues or unbounded retries.
3. Do not increase the upstream 32-packet static pool until measured hardware tests prove it is necessary.
4. Preserve upstream duplicate handling and hop-limit enforcement.
5. 1-byte path mode is collision-prone enough that loop safety must not depend on path-hash loop detection alone.
6. Invalid or over-limit flood packets are dropped before forwarding.
7. A management or GPS failure must never be allowed to disable the LoRa forwarding loop.
8. No production release until physical T-Deck V19 <-> P1 Pro V1 compatibility is proven.

## Three coding phases

### Phase 1 — deterministic base
- pin MeshCore
- apply P1 V1 routing profile
- contract tests
- build only the SenseCAP Solar repeater target
- generate UF2 + SHA-256 manifest
- no automatic release

### Phase 2 — resilience
- bounded congestion/backpressure work
- subsystem health/recovery
- safe config strategy
- power/GPS policy
- diagnostics counters and soak-test hooks

### Phase 3 — release + website
- hardware compatibility matrix
- erase/recovery artifact
- production manifest
- P1 Pro device route on MeshOffGridNL installer
- publish only after hardware acceptance gates pass

## Release gate

P1 Pro V1 is not Stable until all of these pass:

- build and contract tests
- 1/6/12/24/36/48-hop simulation/contract coverage where feasible
- physical TX/RX with T-Deck V19
- T-Deck -> P1 -> T-Deck forwarding
- reboot recovery
- low-power recovery
- duplicate/queue stress
- long-running soak test
- installer recovery test


## Phase 2 implementation status

The resilience branch adds the following without changing the MeshCore wire format:

- fixed 32-packet pool retained
- inbound/outbound queue drop counters
- TX/RX queue high-water marks
- P1 `base health` diagnostics command
- periodic advert suppression under queue pressure
- conservative packet-pool stall recovery:
  - pool must remain fully exhausted
  - traffic counters must show no progress
  - both conditions must persist for 120 seconds
  - only then is a board reboot allowed
- nRF52 power-saving enabled for fresh P1 V1 installs
- GPS remains off by default
- preferences use temp/backup/rename with boot fallback instead of delete-before-write

Phase 2 deliberately does **not**:
- enlarge the packet pool
- increase RF output power
- force CAD/interference thresholds without site measurements
- force continuous GPS
- change the MeshCore packet format
- publish a production release


## V19 radio compatibility lock

A factory-clean P1 Pro V1 install starts on the exact LoRa defaults used by the pinned
MeshOffGridNL T-Deck V19 WadaMesh build:

- frequency: 869.618 MHz
- bandwidth: 62.5 kHz
- spreading factor: SF8
- coding rate: CR5
- TX power target: 22 dBm (hardware/region constraints still apply)

These values are build-time fresh-install defaults only. Persisted user configuration is
not silently overwritten on later boots. The website RC1 flow requires the factory-clean
erase step so first pairing with V19 is deterministic.
