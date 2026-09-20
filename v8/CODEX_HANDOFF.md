# V8 Coding Handoff

Do not start from main and do not modify V7.

Canonical firmware repository:
kevinkok1999/Meshtastic-firmware

Pre-code branch:
v8-architecture-prep

Validated V7 base:
4413758a25dfbc7958d134bbe9b1734243d608fa

## Coding objective

Create V8 as an isolated development branch from v8-architecture-prep.

V8 must:
- preserve all stock Saitama/MeshCore behavior;
- preserve V7 ESP-NOW compatibility;
- generalize routing into a transport-independent coordinator;
- support optional XBee XR 868;
- support optional LR2021;
- use one chat UI;
- automatically select/fallback using measured link health;
- remain fully functional when optional hardware is missing.

## Do not

- modify V1-V7 branches/assets;
- edit production installer during firmware coding;
- hard-code unverified expansion GPIOs;
- send decrypted private chat text as alternate-transport payload;
- assume XBee always has greater range than LoRa;
- assume LR2021/SX1262 interoperability without a physical test;
- launch parallel firmware builds;
- add Wi-Fi HaLow/satellite/backscatter into the first V8 coding milestone.

## Implementation sequence

1. Create v8-dev from v8-architecture-prep.
2. Add host-testable V8 core:
   - message ID
   - envelope
   - fragmentation/reassembly
   - dedupe
   - route metrics/scoring
   - bounded queue
3. Wrap existing V7 ESP-NOW code behind the V8 contract while keeping the V7 wire protocol.
4. Port only the useful XBee codec/link pieces from feature/xbee-xr868-transport.
5. Add XBee unit tests.
6. Add LR2021 adapter using the RadioLib version pinned for V8.
7. Add SubGhzRfArbiter.
8. Integrate at MeshService boundary.
9. Add settings/diagnostics.
10. Run native tests.
11. Run exactly one T-Deck build.
12. Hardware-test STOCK profile before enabling external radios.
13. Hardware-test XBee.
14. Hardware-test LR2021.
15. Only after all gates, prepare V8 installer preview in MeshOffGridNL.

## Build policy

Use:
- one target: t-deck
- one runner
- concurrency group v8-firmware
- cancel-in-progress true

Do not spend the firmware runner on documentation-only changes.

## Success criteria

The first flashable V8 candidate is not considered successful merely because it compiles. It must boot and pass the STOCK regression section of v8/TEST_MATRIX.md on real hardware.
