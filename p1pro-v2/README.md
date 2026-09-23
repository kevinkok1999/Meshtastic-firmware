# MeshOffGridNL P1 Pro V2 — EU868 Adaptive Base Station

Status: Phase 1 candidate.

V2 is intentionally **EU868-only** and stays directly aligned with the proven T-Deck V19 radio profile:

- 869.618 MHz
- 62.5 kHz
- SF8
- CR5
- TX setting capped at 22 dBm
- scoped flood ceiling 48 hops
- unscoped 6
- adverts 8

## What V2 adds over V1

- persisted radio settings are forced back to the V19-compatible EU868 profile
- CLI cannot switch V2 to 915 MHz or another radio profile
- TX commands cannot exceed the 22 dBm hardware target
- baseline airtime budget is limited to 10%
- adaptive congestion states tighten the budget to 5%, 2% or 1%
- CAD is automatically enabled only under mesh pressure
- escalation is immediate; recovery requires 30 seconds of quiet samples
- V1's fixed 32-packet pool, queue telemetry, safe prefs transaction and hard-stall supervisor remain inherited

## Installer contract

Normal V2 installation is designed as:

**Connect -> Install V2**

A normal V1 -> V2 update must not require Factory Clean. Factory erase and UF2 stay available only under Advanced / Recovery.

## Three phases

1. EU868 hard lock + Adaptive Mesh
2. Solar Guardian + staged self-healing + trust/provisioning
3. V2 RC1 release + one-click website installer + physical acceptance

V1 remains untouched and available as fallback.


## Phase 2 — Solar Guardian + Trust + staged recovery

Phase 2 adds:

- runtime power states NORMAL / ECO / CRITICAL / PROTECT
- ECO threshold 3600 mV with 3700 mV recovery hysteresis
- CRITICAL threshold 3450 mV with 3550 mV recovery hysteresis
- hard protection at 3300 mV, matching the upstream SenseCAP boot lock
- two consecutive low readings before runtime protective shutdown
- low-voltage shutdown reuses upstream LPCOMP + VBUS wake
- ECO/CRITICAL suppress only background adverts; LoRa forwarding remains enabled
- CAD AUTO backs off in low-power states unless explicitly enabled
- first boot replaces only the public upstream admin default `password` with a random per-device credential
- an operator-set existing password survives upgrades
- 60-second soft radio recovery before the existing hard reboot safety net
