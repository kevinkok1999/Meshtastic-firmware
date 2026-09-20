# V9 implementation overlay

V9.0 keeps V8 compatibility and adds a production-safe stock-T-Deck Reach Engine.

Actually integrated in MeshService:
- measured ESP-NOW ACK/fallback feedback into routing;
- RangeFirst adaptive primary/backup route decisions;
- bounded route hysteresis and failure scoring;
- automatic ESP-NOW LR <-> LoRa fallback;
- bounded local deferred-send queue when neither transport accepts a message;
- expiry, backoff and retry caps for deferred messages;
- unique V9 128-bit message IDs for queued delivery state.

Compiled and host-tested V9 protocol core:
- 32-fragment selective-ACK bitmap;
- fixed-buffer XOR erasure FEC that can recover one missing fragment per protected block;
- MessageId128 generator.

Compatibility rule:
- V9 does not force the new fragmentation/FEC wire format onto V7/V8 peers.
- Existing MeshCore/LoRa and V7/V8 ESP-NOW traffic stays wire-compatible.
- XBee XR 868 and LR2021 remain optional external hardware paths and are not falsely enabled on stock hardware.

This is deliberately safer than advertising untested external radios as active.
