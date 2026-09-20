# V9 Coding Handoff

Canonical repository:
kevinkok1999/Meshtastic-firmware

Pre-code branch:
v9-architecture-prep

Immutable V8 base:
fbe6917df3f040ab72dc3dee8b82c131cbc8fea5

## Create

Create v9-dev from v9-architecture-prep.

Do not code directly on V8 branches.

## First coding objective

Produce a stock-T-Deck V9 candidate that adds the delivery core without requiring XBee or LR2021 hardware.

Implement in this order:
1. MessageId128.
2. V9 envelope parser/serializer.
3. bounded dedupe/replay cache.
4. FragmentWindow.
5. selective ACK bitmap.
6. fixed-buffer first FEC codec.
7. LinkMetricStore.
8. ReachEngine.
9. PathManager.
10. V8CompatibilityAdapter.
11. built-in LoRa + ESP-NOW integration.
12. optional store-forward RAM queue.
13. host tests.
14. one T-Deck integration build.

Only after STOCK passes:
15. persistent queue.
16. relay.
17. XBee integration.
18. LR2021 profiles.
19. RF coexistence integration.
20. UI diagnostics.

## Interface rules

All core logic must compile in a host/native test target without Arduino radio hardware.

Radio adapters expose measurements; they do not make global path decisions.

Delivery Coordinator owns retries and delivery completion.

## Dependency rule

RadioLib latest observed during V9 preparation is 7.7.1.

Do not blindly change the Saitama dependency.
First:
- pin an exact candidate version;
- compile stock V8/V9;
- run SX1262 regression;
- compile LR2021 adapter;
- record the chosen commit/tag in V9 lock data.

## Safety/reliability stop conditions

Stop promotion if:
- stock T-Deck cannot boot reliably;
- BLE/ESP-NOW coexistence regresses;
- relay can loop;
- queue can grow without bound;
- FEC creates retry/airtime amplification;
- optional hardware absence crashes chat;
- RF self-desense causes repeatable loss;
- a PHY profile exceeds validated regional/module limits.

## Runner policy

Exactly one release firmware build lane:
- concurrency: v9-firmware
- cancel-in-progress: true
- target: t-deck only

Use native tests before firmware CI.

## Website handoff

Do not touch production website during coding.

After real hardware QA:
- create v9-installer-preview;
- add V9 as a new selector without changing V1-V8 entries;
- immutable firmware asset commit;
- exact byte-size check;
- exact SHA-256;
- ESP32-S3/16MB/image validation;
- Vercel preview;
- real T-Deck flash;
- production fast-forward only when all gates pass.
