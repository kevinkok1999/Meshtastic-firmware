# V27 MASTER DESIGN — GLOBAL-FIRST + RF INTELLIGENCE + IMMUTABLE LEGACY

Status: DESIGN ONLY. No implementation is authorized by this document.

## Immutable legacy rule

The following are immutable from the V27 program:
- P1 Pro V8
- V26
- V19
- every earlier T-Deck firmware version
- every earlier installer/release artifact

V27 may read their published protocol/RF contracts and remain compatible through adapters, but it must never patch, rewrite, re-tag, silently migrate or re-release those versions.

V27 has:
- its own branch family;
- its own protocol capability flags;
- its own installer/release channel;
- its own global transport;
- its own RF Intelligence features;
- its own rollback.

Compatibility is one-way engineering responsibility of V27.

## Product identity

MeshOffGridNL V27 is a worldwide messaging platform with two routes:

1. GLOBAL-FIRST — Wi-Fi/Internet is the primary path for worldwide messaging.
2. RF-SECOND — LoRa/MeshCore is the secondary path for off-grid, Internet failure, local long-range delivery and legacy/P1 compatibility.

The user sees:
- one chat;
- one contact/group;
- one send button;
- one message history.

The user never chooses Wi-Fi vs RF.

## Global route design

When a V27 peer is globally reachable:
- encrypt locally;
- send via authenticated secure relay;
- avoid redundant RF transmission;
- receive a real device delivery receipt;
- keep RF available in reserve.

When the global path is unavailable:
- use RF automatically when the destination/path is RF-capable;
- preserve the same logical message ID;
- resume global route automatically when connectivity returns.

Legacy/P1 peers:
- never wait for unsupported Internet capabilities;
- keep the existing RF compatibility path.

## RF Intelligence Layer

V27 does not treat “noise reduction” as one filter. It uses a bounded RF Intelligence Layer.

### RF Health Engine

Rolling bounded observations:
- RSSI
- SNR
- valid-packet ratio
- CRC/auth failure rate
- CAD activity
- retry count
- duplicate count
- receive timeout rate
- estimated local noise floor
- recent peer/path success

Output:
- normalized RF health score;
- link confidence;
- channel occupancy confidence;
- interference class.

### Interference classification

Classify radio conditions as:
- quiet;
- valid LoRa activity;
- broadband/noise activity;
- intermittent burst interference;
- persistent interference.

Do not equate high RSSI with useful LoRa traffic.

### CAD-assisted transmission

For V27 advanced RF:
- use SX1262 Channel Activity Detection where beneficial;
- avoid transmitting directly into a detected LoRa burst;
- bounded random defer;
- no infinite waiting loop;
- RF fallback must remain responsive.

### Controlled AGC / noise-floor recovery

Extend V26 philosophy:
- no continuous AGC resetting;
- no constant recalibration;
- only calibrate after sustained abnormal evidence;
- never calibrate during an active packet;
- enforce minimum interval between recoveries.

### Temporal diversity

On retry:
- same logical message ID;
- varied bounded retry timing;
- receiver deduplicates copies;
- reduces sensitivity to short interference bursts.

### Selective repair for V27-only payloads

Future V27-to-V27 advanced mode:
- fragment sequence numbers;
- missing-fragment bitmap;
- resend only missing pieces;
- optional small parity/FEC block.

Never apply this to P1 V8/legacy packets.

## Dual RF lane architecture

### Lane A — Immutable compatibility lane

For P1 Pro V8 and legacy interoperability.

Contract remains exactly the published compatible profile:
- 869.618 MHz
- 62.5 kHz
- SF8
- CR5
- configured TX ceiling 22 dBm

V27 does not alter the peer firmware or its stored profile.

### Lane B — V27 advanced lane

V27-to-V27 only and capability-negotiated.

Potential tools, subject to regulatory and hardware validation:
- pre-approved alternate legal channel/profile;
- bandwidth/spreading-factor profile selected for link condition;
- stronger coding/redundancy;
- CAD/polite-access behavior;
- selective repair;
- route-aware retry policy.

Lane B is never used unless:
- both peers advertise the exact same V27 capability version;
- regional profile permits it;
- P1/legacy coexistence remains safe;
- validation gates pass.

Failure returns to Lane A or normal global route.

## Spectrum Intelligence

No arbitrary blind frequency hopping.

Maintain a bounded quality map of pre-approved regional RF profiles using:
- occupancy;
- noise floor;
- packet success ratio;
- collision/retry history.

The RF master selects only from profiles approved for the region and hardware.

## Long-range improvements outside DSP

RF range is system-level, not firmware-only.

Design improvements:
- tuned 868-MHz antenna system;
- known connector/cable loss;
- proper antenna orientation;
- physical separation from digital/Wi-Fi noise sources;
- fixed-node antenna height;
- production RF matching/VSWR verification;
- power-supply noise control;
- enclosure placement review.

## Macro-diversity

Optional future V27 infrastructure:
- geographically separated receiving gateways can hear the same encrypted RF message;
- each can upload the same logical packet to the global network;
- global dedup accepts one valid copy;
- no gateway needs plaintext.

This can improve coverage without increasing end-device transmit power.

## Smart relay topology

V27 can score routes by:
- global availability;
- RF path quality;
- last-success age;
- queue pressure;
- battery/resource state.

Priority policy:
1. global direct;
2. global via Master Gateway;
3. local encrypted LAN optimization where explicitly supported;
4. RF compatibility/advanced path;
5. bounded store-and-forward.

Normal users never see this route graph.

## Master Gateway

Optional network appliance:
- upstream 5-GHz Wi-Fi and/or Ethernet;
- downstream highly compatible 2.4-GHz AP;
- DHCP/DNS/NAT;
- automatic WAN recovery;
- secure administration;
- no chat decryption;
- no device private-key custody.

T-Deck sees a normal 2.4-GHz network.

## Global platform uniqueness targets

V27 aims to combine:
- worldwide Internet messaging;
- automatic off-grid RF fallback;
- one unified chat history;
- P1/legacy compatibility without modifying those devices;
- fixed-size E2E encrypted global envelopes;
- signed V27 group messages;
- adaptive 2.4-GHz connectivity;
- optional 5-GHz-to-2.4-GHz Master Gateway;
- RF interference intelligence;
- capability-negotiated V27 long-range lane;
- encrypted store-and-forward;
- multi-region relay;
- macro-diversity;
- transport-independent delivery receipts;
- zero-config route selection.

## Hard “worldwide platform” requirements

Before V27 Stable:
- global relay is authenticated and encrypted in transit;
- at least two relay regions or equivalent failover design;
- offline ciphertext delivery works;
- credential revocation works;
- Internet/Wi-Fi is the primary V27-to-V27 route;
- RF automatically takes over when global path is unavailable;
- P1 V8 bidirectional RF compatibility passes without P1 changes;
- broad 2.4-GHz router matrix passes;
- Master Gateway is optional and transparent;
- no duplicate visible messages;
- no false delivery state;
- long-duration soak passes;
- RF regulatory profile is validated for each shipping region.

## Things V27 must never do

- modify P1 Pro V8;
- modify V26/V19/older releases;
- silently change a legacy RF profile;
- claim software can make ESP32-S3 receive 5-GHz Wi-Fi;
- use arbitrary frequencies outside an approved regional profile;
- continuously recalibrate RF;
- flood RF with every globally delivered message;
- make cloud availability a requirement for local RF chat;
- put service-role/backend secrets in firmware;
- present a public test broker as production infrastructure.
