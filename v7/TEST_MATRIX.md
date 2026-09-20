# V7 Test Matrix

Every release gate is evidence-based. A compile alone is not a pass.

## A. Build and image
- [ ] Clean checkout with recursive submodules builds t-deck.
- [ ] No source dependency floats unexpectedly.
- [ ] App-only binary generated.
- [ ] Merged binary generated at 0x0 layout.
- [ ] ESP32-S3 image recognized.
- [ ] Flash size is 16 MB.
- [ ] Flash mode is DIO.
- [ ] SHA-256 and byte size recorded.
- [ ] Cold boot after full erase succeeds.
- [ ] Reflash succeeds after interrupted/failed attempt recovery procedure.

## B. Saitama/MeshCore regression
- [ ] Home/UI starts.
- [ ] Keyboard/trackball/touch baseline works.
- [ ] Public/channel messaging over LoRa works.
- [ ] Direct message over LoRa works.
- [ ] Advert/contact discovery over LoRa works.
- [ ] Repeaters/path behavior remains functional.
- [ ] GPS/map initialization baseline works.
- [ ] BLE disabled baseline works.
- [ ] BLE companion advertising works.
- [ ] BLE companion connection works.
- [ ] Existing Saitama 1.3.0 settings migrate safely.
- [ ] LORA_ONLY keeps ESP Wi-Fi radio disabled when not otherwise needed.

## C. ESP-NOW LR transport
Use two V7 T-Deck Plus devices.
- [ ] Both enter LR-capable mode.
- [ ] Capability discovery associates the correct contact identity and MAC.
- [ ] Unknown/malformed capability frames are rejected.
- [ ] Direct short payload succeeds.
- [ ] Maximum supported V7 payload succeeds through fragmentation/reassembly.
- [ ] Missing fragment expires cleanly.
- [ ] Duplicate fragment does not duplicate data.
- [ ] ACK success is distinguishable from ESP-NOW link-layer callback.
- [ ] ACK timeout retries are bounded.
- [ ] Peer loss does not lock the task/UI.
- [ ] Reboot clears/recovers volatile peer state correctly.
- [ ] No plaintext private-message body appears in discovery or raw V7 transport framing.

## D. Hybrid policy
- [ ] AUTO + reachable V7 peer chooses ESP-NOW path.
- [ ] AUTO + non-V7 MeshCore peer uses LoRa.
- [ ] AUTO + ESP-NOW timeout falls back to LoRa.
- [ ] LORA_ONLY never transmits ESP-NOW.
- [ ] ESPNOW_LR_ONLY never silently transmits LoRa.
- [ ] REDUNDANT produces one UI message at receiver, not duplicates.
- [ ] Cross-transport dedup survives reordered arrival.
- [ ] Route indicator reports actual successful route.

## E. Coexistence/stability
Run ESP-NOW tests with:
- [ ] BLE disabled.
- [ ] BLE enabled/advertising.
- [ ] BLE connected.
- [ ] Display active.
- [ ] Display sleeping/waking.
- [ ] GPS enabled.
- [ ] SD/map activity.
Check:
- [ ] no watchdog reset,
- [ ] no heap growth/leak pattern,
- [ ] no queue starvation,
- [ ] no UI freeze,
- [ ] LoRa receive returns after ESP-NOW use.

## F. Compatibility
- [ ] V7 ↔ unmodified Saitama/MeshCore node works over LoRa.
- [ ] V7-specific control frames are not sent on LoRa in a way that harms legacy peers.
- [ ] V7 can message through a normal MeshCore repeater using existing LoRa behavior.
- [ ] No claim of Meshtastic interoperability is introduced; MeshCore and Meshtastic remain separate protocols.

## G. Power sanity
Compare against V4/Saitama baseline:
- [ ] LORA_ONLY idle draw is not materially regressed by accidentally leaving Wi-Fi on.
- [ ] AUTO idle behavior documented.
- [ ] ESP-NOW active cost documented.
No release blocker threshold should be invented before hardware measurements exist; record actual observations.

## H. Installer/release
- [ ] V7 release asset URL immutable/pinned.
- [ ] Website proxy validates expected byte size.
- [ ] Browser validates SHA-256 before erase/flash.
- [ ] Installer detects ESP32-S3 + 16MB.
- [ ] Read-back verification passes.
- [ ] V1-V6 selection and metadata unchanged.
- [ ] NL/EN/DE selector parity.
- [ ] Vercel preview tested before production.
- [ ] Real T-Deck Plus flashed successfully from preview.
