# V8 implementation overlay

This overlay is applied after the validated V7 overlay to the exact Saitama v1.3.0 source.

Implemented in the first V8 coding pass:
- transport-independent adaptive route core;
- explicit AUTO/RANGE_FIRST/POWER_SAVE and single-transport policy modes;
- delivery-probability, latency, queue, energy and failure metrics;
- sticky hysteresis to avoid route flapping;
- V7 ESP-NOW LR compatibility retained;
- normal MeshCore/LoRa fallback retained;
- XBee XR 868 API codec and non-blocking UART link driver;
- LR2021 RadioLib adapter scaffold behind OPS_V8_ENABLE_LR2021;
- XBee/LR2021 remain unavailable by default until safe external hardware pins are configured and validated.

The stock downloadable V8 image therefore improves the built-in LoRa + ESP-NOW routing while carrying the external-radio code seam without guessing T-Deck expansion pins.
