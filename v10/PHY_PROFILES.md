# V10 Validated PHY Profiles

V10 code may select only named profiles.

No free-form user RF register editor is part of V10.

## LoRa

Keep V9/MeshCore compatibility profile as the baseline.

Additional range profiles may be benchmarked later, but each must satisfy:
- EU868 constraints;
- peer interoperability;
- acceptable airtime;
- measured improvement.

## GFSK Direct

Initial coding profiles are conservative proof profiles, not final performance claims.

GFSK_PROBE:
- short preamble;
- compact probe;
- conservative bitrate/deviation/rx bandwidth chosen from RadioLib-supported SX1262 values.

GFSK_DATA_ROBUST:
- lower bitrate;
- stronger preamble/sync margin;
- bounded payload.

GFSK_DATA_FAST:
- only after bench validation.

The exact numerical values are frozen only after two-device packet-error-rate measurements.

## ESP-NOW LR

Keep current V7-compatible LR mode as baseline.

V10 adds:
- pair-specific metrics;
- MessageId128 mapping;
- scheduler ownership;
- adaptive fallback policy.

## Wi-Fi LR Direct

Use Espressif LR protocol only when both peers negotiated V10 Wi-Fi LR capability.

V10 carries DirectLink frames over a minimal UDP session.
No internet, DNS, MQTT or router is involved.

## BLE Coded

Two candidate classes:
- CODED_S2
- CODED_S8

Selection requires actual controller/stack support in the exact Arduino/ESP-IDF package.

Prefer BLE Coded only when its range benefit is needed because its longer airtime can reduce concurrent Wi-Fi performance.

## Regulatory guardrail

Profile manager may:
- select;
- reduce;
- back off.

It may not exceed validated regional/module limits in pursuit of range.
