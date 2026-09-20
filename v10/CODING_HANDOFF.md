# V10 Coding Handoff

Repository:
kevinkok1999/Meshtastic-firmware

Preparation branch:
v10-architecture-prep

Base:
d24bd9910b53589652778ff79fc7da795d790c07

## Create next

Create v10-dev from v10-architecture-prep.

Do not modify v9-dev, v9-assets or website production.

## First source commit

The first V10 coding commit should contain only host-testable core:
- V10Types
- PairSession
- DirectLinkFrame
- LinkMetricStore
- ProbePlanner
- RFResourceScheduler interface/mock
- DirectLinkBrain
- tests

Do not add a firmware workflow until host tests pass.

## Second source commit

Integrate known hardware paths:
- LoRa compatibility;
- ESP-NOW LR;
- unified MessageId128;
- DirectLink delivery coordinator.

Then create the single-runner V10 workflow and perform one T-Deck build.

## Third technical spike

GFSK first.

Reason:
- it uses hardware already controlled by firmware;
- RadioLib 7.7.1 already exposes SX126x GFSK;
- it exercises the critical radio scheduler/rollback design before adding more 2.4-GHz coexistence complexity.

Do not merge GFSK into default routing until physical A/B transition tests pass.

## Fourth spike

Wi-Fi LR direct.

Implement deterministic AP/STA role election and minimal authenticated UDP transport.
Prove restoration to ESP-NOW.

## Fifth spike

BLE Coded.

The exact V9 build uses Arduino ESP32 package 4.20017.260907.
The BLE implementation must detect the underlying ESP-IDF API/version at compile time.

Do not assume the latest ESP-IDF function spelling is present.
Do not replace the whole Arduino framework merely to obtain Coded PHY unless a separate regression branch proves it is necessary.

## Dependency pinning

Saitama declares RadioLib ^7.6.0 but the successful V9 build resolved 7.7.1.

V10 must pin the exact RadioLib release/commit used for GFSK testing before release.

Also pin the PlatformIO Espressif32/Arduino framework after capability proof so future CI does not silently change BLE/Wi-Fi behavior.

## Release truth

"Builds" is not "100% works".

V10 is release-ready only when:
- two real T-Deck Plus units boot;
- A<->B works on LoRa/ESP-NOW;
- each new bearer that is advertised has passed physical A/B testing;
- automatic fallback returns to a working bearer;
- no radio mode transition strands SX1262 or ESP32 RF state.
