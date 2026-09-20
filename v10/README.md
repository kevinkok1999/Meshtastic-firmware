# MeshOffGridNL V10 — DirectLink Extreme

Status: CODING-READY PREPARATION
Prepared: 2026-09-20
Base firmware commit: d24bd9910b53589652778ff79fc7da795d790c07
Target: LILYGO T-Deck Plus, two-device direct communication only.

## Product goal

V10 is optimized for exactly two standard T-Deck Plus devices:

T-Deck A <-> T-Deck B

No relay, repeater, backbone or third node is required.

V10 uses only hardware already present in the T-Deck Plus:
1. SX1262 LoRa;
2. SX1262 GFSK direct mode;
3. ESP32-S3 ESP-NOW Long Range;
4. ESP32-S3 Wi-Fi Long Range direct IP link;
5. ESP32-S3 BLE LE Coded PHY.

These are five logical bearers but only two physical RF resources:
- SUBGHZ: SX1262, LoRa or GFSK at one time;
- RF24: ESP32-S3 2.4 GHz radio, ESP-NOW/Wi-Fi/BLE share coexistence and airtime.

V10 never assumes these routes can all transmit simultaneously.

## Design priorities

1. maximum A-to-B delivery reliability;
2. long-range direct delivery before speed;
3. no hardware modification;
4. no dependency on internet or other nodes;
5. one chat UI;
6. automatic route selection;
7. bounded airtime, retries, RAM and battery use;
8. fallback to known-working V9 behavior if new V10 bearers are unavailable.

## Historical integrity

V1-V9 are immutable.

Do not change production website during V10 architecture/coding.
V10 is published only after build validation and real two-device hardware QA.
