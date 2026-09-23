# MeshOffGridNL P1 Pro V2 — EU868 Adaptive Base Station

Status: Phase 1 candidate. V1/RC1 remains untouched and is the rollback line.

## Non-negotiable radio contract

P1 Pro V2 is an **EU868-only** product line and remains compatible with the proven T-Deck V19 radio defaults:

- frequency: 869.618 MHz
- bandwidth: 62.5 kHz
- spreading factor: SF8
- coding rate: CR5
- TX power ceiling: 22 dBm
- scoped flood ceiling: 48
- unscoped flood ceiling: 6
- advert flood ceiling: 8
- path hashes: 1 byte

V2 rejects persistent or temporary radio commands that attempt to move the station away from this radio profile. TX power may be reduced but never raised above 22 dBm.

## Phase 1 — EU868 Adaptive Mesh

V2 inherits the proven V1 base and resilience overlays, then adds:

- EU868/V19 radio hard lock after persisted configuration is loaded
- remote/local CLI guards for radio, frequency, temporary-radio and TX-power changes
- four pressure levels based on bounded pool/queue occupancy
- background adverts suppressed from BUSY upward
- hardware CAD automatically enabled only at SEVERE/CRITICAL pressure, while an operator may still explicitly enable CAD earlier
- no packet-pool enlargement
- no increase in hop ceiling or TX power
- no protocol-format change

Pressure levels:

- 0 NORMAL
- 1 BUSY
- 2 SEVERE
- 3 CRITICAL

48 hops remains an emergency ceiling, never a routing target.

## Three V2 phases

1. EU868 Adaptive Mesh
2. Solar Guardian + staged self-healing + trust/provisioning
3. RC1 release + one-click P1 installer + production website

V2 is never promoted to Stable until physical P1 Pro <-> T-Deck V19 acceptance testing succeeds.
