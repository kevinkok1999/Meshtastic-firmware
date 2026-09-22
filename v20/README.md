# MeshOffGridNL V20 — Local AI Core

V20 is designed as a strict layer on top of the validated V19 Factory Clean Wi-Fi baseline for the LILYGO T-Deck / T-Deck Plus.

## Goal

Add a fully local, offline-capable intelligence layer to the T-Deck without turning the ESP32-S3 into an LLM host and without regressing V19 Wi-Fi, LoRa, UI, storage, or mesh behavior.

V20 must remain useful with:
- no internet;
- no cloud AI;
- only the T-Deck's ESP32-S3, 16 MB flash, 8 MB PSRAM, microSD, keyboard, touch, GPS, Wi-Fi and SX1262 radio.

## Product behavior

The user opens **Mesh AI**, types a short natural command, and receives a local answer or a proposed device action.

Examples:
- "wie is bereikbaar?"
- "waarom is wifi niet verbonden?"
- "hoe is mijn radioverbinding?"
- "toon mijn locatie"
- "welke route is het beste?"
- "stuur mijn locatie naar <contact>"
- "open chats"
- "toon batterij"

V20 is not a ChatGPT clone. It is a compact local assistant specialized for this device and network.

## V20 design rules

1. **V19 is the immutable base.** All V16-V19 Wi-Fi race/crash hardening and V19 factory-clean behavior remain present.
2. **AI never owns the radio or Wi-Fi drivers.** It consumes state and calls existing public actions through adapters.
3. **No autonomous sending.** Any action that transmits user content or location is executed only after an explicit user command and a validated target.
4. **Local-first.** Core intent parsing, diagnostics, route advice and device control work without internet.
5. **Bounded resources.** No large language model, no unbounded history, no inference loop that can starve radio/UI tasks.
6. **Fail-open for communications.** If Mesh AI crashes or is disabled, normal chat/radio/Wi-Fi behavior must continue.
7. **One-runner CI.** V20 uses one workflow, one firmware build, and one ordered test pipeline.

## V20 scope

### Included in V20
- Mesh AI full-screen UI launched from Home.
- Local command/intent parser.
- Device/network context snapshot.
- Wi-Fi diagnostics that understand V19 connection phases.
- LoRa/mesh link health summary from existing RSSI/SNR/path/ACK data.
- Route Advisor using deterministic scoring and recent local observations.
- Local command history and route statistics on SD when available.
- Explicit send/location actions through safe adapters.
- Feature flags for optional tiny ML and voice work without making them release blockers.

### Not a V20 release blocker
- General-purpose local LLM.
- Cloud AI.
- Fully autonomous message composition.
- A new internet messaging backend.
- Changes to LoRa PHY/radio protocol.
- Voice recognition as a required feature.

## Delivery plan

V20 will be implemented in three coding phases only after this design is accepted:

1. **Core + contracts** — context, intents, policy, storage, diagnostics, tests.
2. **UI + action adapters** — Mesh AI screen, keyboard flow, safe device actions, route advisor.
3. **Build + release** — V19 regression tests, one T-Deck build, manifest/release assets, website installer integration as Version 20.

Physical T-Deck validation remains mandatory before marking V20 Stable.
