# V8 Test Matrix

## A. Immutable baseline
- [ ] V7 release branch/head unchanged.
- [ ] V1-V7 release assets unchanged.
- [ ] V8 branch ancestry points to validated V7 base.
- [ ] Production installer unchanged during firmware development.

## B. Stock T-Deck regression
- [ ] Cold boot.
- [ ] Display.
- [ ] Keyboard.
- [ ] Trackball/touch.
- [ ] SD/map baseline.
- [ ] GPS baseline.
- [ ] LoRa channel message.
- [ ] LoRa direct message.
- [ ] contact discovery.
- [ ] BLE advertising.
- [ ] BLE connected.
- [ ] no watchdog reset.
- [ ] no persistent heap growth.

## C. V7 compatibility
- [ ] V8 understands V7 ESP-NOW capability/DM path.
- [ ] V7 peer does not need V8 capability.
- [ ] V8 does not send V8-only frames to a V7 peer without negotiation.
- [ ] fallback to LoRa works.

## D. V8 message identity
- [ ] two identical texts get different message IDs.
- [ ] same message over two transports is delivered once.
- [ ] reordered duplicate arrival is delivered once.
- [ ] reboot/session change does not create a practical collision.
- [ ] bounded dedupe cache expiry works.

## E. Route policy
- [ ] unavailable links are rejected.
- [ ] stale metrics are rejected.
- [ ] MTU mismatch is rejected.
- [ ] encryption requirement cannot be bypassed.
- [ ] hysteresis prevents route flapping.
- [ ] RANGE_FIRST uses measured link health, not a fixed technology ranking.
- [ ] POWER_SAVE respects delivery floor.
- [ ] REDUNDANT sends at most two copies and receiver shows one message.

## F. ESP-NOW LR
- [ ] direct V8 peer delivery.
- [ ] application ACK.
- [ ] bounded retry.
- [ ] peer expiry.
- [ ] BLE disabled test.
- [ ] BLE advertising test.
- [ ] BLE connected test.

## G. XBee XR 868
- [ ] module absent -> safe disabled state.
- [ ] API parser checksum rejection.
- [ ] identity/address query.
- [ ] runtime NP/MTU query.
- [ ] receive packet.
- [ ] TX status success.
- [ ] TX status failure.
- [ ] non-blocking UART parser.
- [ ] fragmented V8 message.
- [ ] end-to-end protected DM.
- [ ] unplug/reconnect recovery.

## H. LR2021
- [ ] module absent -> safe disabled state.
- [ ] driver init.
- [ ] SPI sharing/arbitration.
- [ ] EU868 profile.
- [ ] TX/RX V8 envelope.
- [ ] application ACK.
- [ ] standard-LoRa compatibility test with SX1262.
- [ ] advanced PHY remains disabled unless explicitly selected/tested.

## I. Sub-GHz coexistence
- [ ] SX1262 and XBee cannot TX simultaneously.
- [ ] SX1262 and LR2021 cannot TX simultaneously.
- [ ] XBee and LR2021 cannot TX simultaneously.
- [ ] arbiter queue timeout is bounded.
- [ ] RF-desense measurements recorded.
- [ ] antenna placement measurements recorded.

## J. Queue/failure behavior
- [ ] bounded send queue.
- [ ] bounded receive queue.
- [ ] queue-full reason recorded.
- [ ] retry/backoff bounded.
- [ ] transport failure cannot freeze UI.
- [ ] route fallback cannot create a duplicate chat entry.

## K. Build/release
- [ ] one T-Deck CI build only.
- [ ] app binary generated.
- [ ] merged binary generated.
- [ ] ESP32-S3 image verified.
- [ ] 16 MB / DIO verified.
- [ ] byte size recorded.
- [ ] SHA-256 recorded.
- [ ] full erase + flash boot passes.
- [ ] interrupted flash recovery passes.

## L. Installer preview
- [ ] V8 preview selector.
- [ ] immutable asset.
- [ ] expected byte size check.
- [ ] SHA-256 check before flash.
- [ ] V1-V7 untouched.
- [ ] NL/EN/DE parity.
- [ ] Vercel preview works.
- [ ] real T-Deck flash from preview works.
