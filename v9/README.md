# MeshOffGridNL Version 9 — Reach Engine

Status: CODING-READY PREPARATION
Prepared: 2026-09-20
Base firmware commit: fbe6917df3f040ab72dc3dee8b82c131cbc8fea5
Base product: validated V8 T-Deck Plus build

## Objective

V9 is a range-and-delivery upgrade, not merely another radio.

V9 keeps the single-chat experience and adds a Reach Engine that can:
- choose the best bearer and path from measured link quality;
- adapt packet size and retry behavior to poor links;
- use selective repeat instead of resending whole messages;
- optionally add bounded erasure-FEC on weak links;
- maintain primary and backup routes;
- store encrypted envelopes temporarily when no end-to-end path exists;
- forward protected envelopes across compatible V9 relays;
- remain compatible with V8/V7 and normal MeshCore/LoRa peers.

Target transport set:
1. onboard SX1262 / MeshCore LoRa;
2. ESP-NOW Long Range on ESP32-S3;
3. optional Digi XBee XR 868;
4. optional Semtech LR2021.

No fifth radio is added to V9 core. Complexity budget is spent on making the four routes work together better.

## Critical principle

Maximum range is not equal to maximum transmit power.

V9 optimizes end-to-end delivery using:
- link margin;
- packet delivery history;
- expected airtime;
- congestion;
- route diversity;
- retries;
- forward error recovery;
- relay placement/quality;
- antenna/hardware quality;
- regional RF constraints.

## Immutable history

Do not modify or republish V1-V8 assets as part of V9 development.

The production installer stays unchanged until V9 has:
1. passed host tests;
2. compiled in the single firmware build lane;
3. booted on real T-Deck Plus hardware;
4. passed two-device messaging regression;
5. passed preview-installer flashing.
