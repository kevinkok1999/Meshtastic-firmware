# V9 Team Coordination

One firmware build lane exists. Parallel work is allowed only for analysis, host tests and isolated source modules.

## Specialist A — Reach Engine
Owns:
- route/path cost;
- normalized metrics;
- hysteresis;
- primary/backup selection;
- recovery ladder.

## Specialist B — Delivery Protocol
Owns:
- MessageId128;
- envelope;
- fragmentation;
- selective ACK;
- dedupe/replay;
- FEC interface.

## Specialist C — DTN/Relay
Owns:
- store-forward queue;
- persistence journal;
- expiry;
- relay policy;
- loop prevention;
- rate limits.

## Specialist D — ESP32-S3 / ESP-NOW
Owns:
- LR capability;
- coexistence;
- link metrics;
- V7/V8 compatibility.

## Specialist E — XBee
Owns:
- API mode;
- module profiles;
- TX status;
- DigiMesh metrics;
- UART lifecycle.

## Specialist F — LR2021
Owns:
- RadioLib pin;
- PHY profiles;
- CAD;
- advanced PHY experiments;
- interoperability bench plan.

## Specialist G — RF/Power
Owns:
- SubGhzRfArbiter;
- antenna/RF-board contract;
- desense tests;
- power integrity;
- regional profile guardrails.

## Specialist H — Security
Owns:
- protected-payload boundary;
- signed capabilities;
- replay protection;
- relay authentication;
- no-plaintext-forwarding rule.

## Specialist I — QA/CI/Installer
Owns:
- host tests;
- test matrix;
- one-runner workflow;
- immutable assets;
- installer preview;
- Vercel validation;
- historical-version integrity.

## Collaboration rules

1. Shared interfaces are frozen before radio integration.
2. No specialist edits another transport's low-level driver without review.
3. No UI logic contains radio policy.
4. No radio driver owns global retry/fallback.
5. STOCK regression blocks all optional-hardware promotion.
6. Documentation and host tests do not consume the firmware runner.
7. Production installer changes occur only after real-device QA.
