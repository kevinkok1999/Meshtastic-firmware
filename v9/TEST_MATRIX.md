# V9 Test Matrix

## A. Baseline immutability
- [ ] V8 firmware commit unchanged.
- [ ] v8-assets unchanged.
- [ ] production installer V1-V8 unchanged during V9 coding.
- [ ] V9 ancestry verified.

## B. Stock T-Deck regression
- [ ] boot.
- [ ] display.
- [ ] keyboard.
- [ ] trackball/touch.
- [ ] SD.
- [ ] GPS.
- [ ] BLE advertise/connect.
- [ ] LoRa channel.
- [ ] LoRa DM.
- [ ] ESP-NOW LR DM.
- [ ] no watchdog reset.
- [ ] bounded heap use.

## C. Compatibility
- [ ] V9 <-> V9.
- [ ] V9 <-> V8.
- [ ] V9 <-> V7 where supported.
- [ ] V9 <-> legacy MeshCore over LoRa.
- [ ] feature negotiation never sends unsupported V9 frame.

## D. Message identity/security
- [ ] identical texts have unique IDs.
- [ ] reboot collision test.
- [ ] duplicate multipath arrival delivered once.
- [ ] replay rejected.
- [ ] invalid auth rejected.
- [ ] signed capability expiry.
- [ ] relay never requires plaintext.

## E. Fragmentation/selective ACK
- [ ] one missing fragment -> only missing fragment resent.
- [ ] multiple missing fragments.
- [ ] reordered fragments.
- [ ] duplicate fragments.
- [ ] fragment timeout.
- [ ] maximum fragment count.
- [ ] malformed bitmap rejected.
- [ ] bounded sender/receiver windows.

## F. FEC
- [ ] no-loss decode.
- [ ] recover one missing fragment in protected block.
- [ ] two missing fragments -> deterministic failure for first codec.
- [ ] corrupted parity rejected.
- [ ] policy disables FEC on strong link.
- [ ] policy enables FEC only inside airtime budget.

## G. Reach Engine
- [ ] stale metrics rejected.
- [ ] raw unlike-RSSI not directly compared.
- [ ] hysteresis prevents flapping.
- [ ] failed primary switches to backup.
- [ ] three strong hops may beat weak direct path.
- [ ] redundant mode max two paths.
- [ ] retry ladder bounded.
- [ ] no-route becomes store-forward only when allowed.

## H. Store-and-forward
- [ ] bounded item count.
- [ ] bounded bytes.
- [ ] expiry.
- [ ] priority.
- [ ] reboot restore.
- [ ] interrupted journal write recovery.
- [ ] CRC/integrity failure isolated.
- [ ] compaction.
- [ ] queue full reason.
- [ ] flash write-rate limit.

## I. Relay
- [ ] TTL/hop budget.
- [ ] loop prevention.
- [ ] dedupe.
- [ ] per-peer/rate limit.
- [ ] forward only valid protected envelope.
- [ ] no relay starvation of local messages.

## J. ESP-NOW LR
- [ ] peer capability.
- [ ] direct ACK.
- [ ] poor-link retry profile.
- [ ] backup to LoRa.
- [ ] BLE advertising coexistence.
- [ ] BLE connected coexistence.
- [ ] Wi-Fi LR only used with compatible peer.

## K. XBee XR 868
- [ ] absent safe state.
- [ ] API checksum.
- [ ] module query.
- [ ] MTU query.
- [ ] TX status.
- [ ] 10/80 kbps profile test.
- [ ] unplug/reconnect.
- [ ] DigiMesh delivery metrics.
- [ ] RF arbiter respected.

## L. LR2021
- [ ] absent safe state.
- [ ] exact RadioLib version pinned.
- [ ] EU868 baseline LoRa profile.
- [ ] SX1262 interoperability bench test.
- [ ] CAD metric.
- [ ] advanced PHY behind feature flag.
- [ ] FLRC/LR-FHSS only after profile-specific tests.
- [ ] RF arbiter respected.

## M. RF/power
- [ ] coexistence bench matrix.
- [ ] brownout under peak TX.
- [ ] current measurements.
- [ ] antenna arrangement documented.
- [ ] no simultaneous local sub-GHz TX.
- [ ] profile power limits enforced.

## N. Build/release
- [ ] native tests green.
- [ ] one T-Deck build.
- [ ] ESP32-S3 verified.
- [ ] 16 MB verified.
- [ ] DIO verified.
- [ ] app and merged binary.
- [ ] SHA-256.
- [ ] byte size.
- [ ] full erase + flash boot.
- [ ] interrupted flash recovery.
- [ ] V9 installer preview.
- [ ] NL/EN/DE parity.
- [ ] real flash from preview.
- [ ] production only after device QA.
