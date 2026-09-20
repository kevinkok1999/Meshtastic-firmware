# V10 DirectLink Architecture

## 1. Layering

Chat UI
 -> DirectLinkDeliveryCoordinator
    -> PairSession
    -> DirectLinkBrain
       -> LinkMetricStore
       -> ProbePlanner
       -> RecoveryPolicy
       -> RFResourceScheduler
    -> Bearer adapters
       -> LoRaCompatBearer
       -> GfskDirectBearer
       -> EspNowLrBearer
       -> WifiLrDirectBearer
       -> BleCodedBearer
 -> Delivery state / diagnostics

Only DirectLinkBrain chooses a bearer.
Only RFResourceScheduler grants physical-radio mode transitions.

## 2. Exactly two peers

V10 has one paired remote identity.

PAIR_NONE
PAIR_DISCOVERED
PAIR_VERIFIED
PAIR_LOCKED

Once PAIR_LOCKED:
- normal V10 direct discovery is addressed to the paired peer;
- metrics are kept only for self + paired peer;
- route memory is bounded and tiny;
- no topology graph or relay table is maintained.

Legacy MeshCore contacts may remain visible for compatibility, but DirectLink Extreme policy optimizes only the locked peer.

## 3. Physical RF scheduler

Two resource locks:

SUBGHZ_LOCK:
- LoRa;
- GFSK.

RF24_LOCK:
- ESP-NOW LR;
- Wi-Fi LR;
- BLE Coded.

States are explicit:
IDLE
PROBING
TX
WAIT_ACK
RESTORING
COOLDOWN

Every transition has:
- deadline;
- rollback function;
- failure counter;
- known-safe home mode.

Home modes:
- SUBGHZ -> LoRa/MeshCore receive;
- RF24 -> ESP-NOW LR discovery/receive when enabled.

No bearer may leave the radio in an unknown state.

## 4. DirectLink Brain

Inputs per bearer:
- available;
- peer-capable;
- last probe age;
- recent ACK success;
- packet-loss EWMA;
- latency EWMA;
- retry EWMA;
- link class;
- estimated airtime;
- energy class;
- transition cost;
- cooldown/failure streak.

Outputs:
- primary bearer;
- backup bearer;
- probe plan;
- retry budget;
- fragmentation profile;
- FEC profile;
- optional second-path retry.

V10 never compares unlike raw RSSI values directly.

## 5. Range-first policy

Default mode: DIRECT_RANGE_FIRST.

Priority:
1. estimated delivery probability;
2. robustness to recent failures;
3. physical-route diversity for backup;
4. airtime;
5. latency;
6. energy.

A slower route may win when its measured delivery confidence is higher.

## 6. Route diversity

Logical bearers are tagged by physical failure domain:

SUBGHZ:
- LoRa
- GFSK

RF24_WIFI:
- ESP-NOW LR
- Wi-Fi LR

RF24_BLE:
- BLE Coded

For recovery, a backup on a different physical RF resource is preferred where scores are close.

Example:
Primary ESP-NOW LR -> backup LoRa
is more independent than
Primary ESP-NOW LR -> backup Wi-Fi LR.

## 7. No uncontrolled redundant blasting

V10 does not transmit every message over every bearer.

Normal:
- primary only.

If no ACK:
- selective retry;
- alternate bearer if confidence justifies;
- optional different physical RF resource for high-priority message.

All retry ladders are bounded.

## 8. Fail-safe rule

If any new V10 bearer fails initialization:
- mark it unavailable;
- continue with V9-compatible LoRa + ESP-NOW LR.

V10 must never require BLE Coded, Wi-Fi LR or GFSK in order to boot or chat.
