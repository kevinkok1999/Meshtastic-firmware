# V9 RF and Hardware Plan

## 1. Stock T-Deck Plus profile

Mandatory baseline:
- onboard SX1262 / MeshCore;
- ESP32-S3 ESP-NOW LR;
- display/keyboard/trackball/touch/SD/GPS unchanged.

V9 must be fully usable with no external radio attached.

## 2. ESP-NOW LR

Use only when peer capability confirms support.

Optimize:
- peer reachability cache;
- delivery EWMA;
- bounded retries;
- channel/coexistence diagnostics;
- 2.4 GHz link confidence.

Do not claim sub-GHz antenna improvements affect ESP-NOW LR.

## 3. XBee XR 868

Optional external path.

V9 requirements:
- API mode;
- non-blocking UART;
- runtime module identity;
- runtime MTU;
- TX status integration;
- 10 kbps / 80 kbps profile support only after measured policy testing;
- expose DigiMesh route health as transport metrics where available;
- preserve module Listen-Before-Talk/frequency-agility behavior.

No fixed GPIO assignment until an expansion design is validated.

## 4. LR2021

Optional external advanced path.

Targets:
- EU868 reference-hardware-compatible design;
- RadioLib-supported integration;
- exact library version pinned after compile/regression validation;
- initial LoRa compatibility profile;
- advanced PHY experiments behind feature flags.

Candidate advanced features:
- improved CAD;
- multi-SF receive where useful;
- LR-FHSS experiments;
- FLRC for higher-rate short/medium links;
- 2.4 GHz LR2021 experiments only on hardware with a validated RF path.

Do not activate a PHY because it is theoretically available; each profile needs interoperability, range, energy and regulatory tests.

## 5. PHY profile manager

V9 defines named validated profiles instead of arbitrary runtime register tweaking.

Each profile contains:
- bearer;
- frequency/channel plan;
- bandwidth;
- spreading/modulation parameters;
- coding parameters;
- legal TX power cap;
- expected MTU;
- airtime model ID.

Only validated profiles can be selected by Reach Engine.

## 6. Antenna strategy

Software cannot compensate for poor antenna/RF design.

V9 expansion-hardware goals:
- separate matched antenna path per radio unless an engineered RF switching network is used;
- correct 50-ohm layout;
- adequate ground reference;
- SAW/filtering where module/reference design requires;
- physical separation to reduce self-desense;
- strain-safe external connector placement.

No software option may imply that a longer antenna is automatically better.

## 7. Optional diversity

Antenna diversity is a future hardware capability, not assumed on the stock T-Deck.

If a validated RF switch/diversity board exists later, expose it as:
- ANT_A
- ANT_B
- AUTO_DIVERSITY

AUTO_DIVERSITY chooses from measured receive quality and packet success, not arbitrary switching.

## 8. Power integrity

External radio board must document:
- rail voltage;
- peak TX current;
- regulator margin;
- decoupling;
- power-enable behavior;
- brownout test.

Firmware:
- optional module power gating;
- wake delay;
- brownout/error counters;
- never pretends to power-gate hardware that lacks a switch.

## 9. Coexistence bench matrix

Required before external radios are release-enabled:
- SX1262 TX / XBee RX;
- XBee TX / SX1262 RX;
- SX1262 TX / LR2021 RX;
- LR2021 TX / SX1262 RX;
- XBee TX / LR2021 RX;
- LR2021 TX / XBee RX;
- ESP-NOW activity during each sub-GHz path;
- BLE connected while ESP-NOW active.

Record:
- packet error rate;
- RSSI/SNR shift;
- reset/brownout;
- current draw;
- antenna arrangement.

## 10. Regional guardrail

All runtime profiles remain inside validated European module/profile limits.

Reach Engine may reduce power or choose another path, but may not exceed profile limits to chase range.
