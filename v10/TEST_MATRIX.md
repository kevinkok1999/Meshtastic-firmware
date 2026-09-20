# V10 DirectLink Test Matrix

## Base integrity
- [ ] V9 source unchanged.
- [ ] v9-assets unchanged.
- [ ] production installer V1-V9 unchanged before V10 release.
- [ ] V10 branch ancestry correct.

## Pair
- [ ] A discovers B.
- [ ] B discovers A.
- [ ] authenticated capability exchange.
- [ ] pair lock.
- [ ] wrong peer rejected.
- [ ] pair survives reboot when enabled.

## Message identity
- [ ] identical text gets distinct MessageId128.
- [ ] same message across two bearers delivered once.
- [ ] replay rejected.
- [ ] delayed duplicate rejected.

## Core policy
- [ ] fresh healthy primary sends immediately.
- [ ] stale metrics trigger bounded probes.
- [ ] primary failure selects backup.
- [ ] different physical failure domain preferred when appropriate.
- [ ] transition cost prevents route flapping.
- [ ] retry budget bounded.

## SX1262 LoRa
- [ ] V9 LoRa regression.
- [ ] receive restored after every GFSK experiment.
- [ ] no BUSY/IRQ lockup.

## SX1262 GFSK
- [ ] proof profile compile.
- [ ] A->B.
- [ ] B->A.
- [ ] probe ACK.
- [ ] data ACK.
- [ ] 100 repeated LoRa->GFSK->LoRa cycles.
- [ ] forced transition timeout recovery.
- [ ] hard reinit fallback.

## ESP-NOW LR
- [ ] V9 compatibility.
- [ ] A->B/B->A.
- [ ] application ACK.
- [ ] fallback on timeout.
- [ ] restore after Wi-Fi LR.
- [ ] restore after BLE window.

## Wi-Fi LR Direct
- [ ] deterministic AP/STA roles match.
- [ ] private pair-derived session.
- [ ] connect timeout bounded.
- [ ] UDP probe.
- [ ] encrypted data.
- [ ] disconnect restore.
- [ ] one controlled role-swap.
- [ ] no internet/router dependency.

## BLE Coded
- [ ] build-time capability.
- [ ] controller capability.
- [ ] Coded PHY negotiated.
- [ ] S2 physical A/B.
- [ ] S8 physical A/B if supported.
- [ ] delivery ACK.
- [ ] Wi-Fi coexistence measured.
- [ ] ESP-NOW restored.

## Fragment/FEC
- [ ] selective resend only missing fragments.
- [ ] XOR1 one-erasure recovery.
- [ ] FEC disabled on strong link.
- [ ] no airtime amplification loop.

## Two-device field tests
For every active bearer:
- [ ] 1 m sanity.
- [ ] indoor rooms/walls.
- [ ] dense built environment.
- [ ] open line of sight.
- [ ] device orientation A.
- [ ] device orientation B.
- [ ] packet delivery ratio.
- [ ] median/p95 delivery latency.
- [ ] retries/message.
- [ ] battery impact sample.

Do not publish guaranteed distance claims from a single environment.

## Release
- [ ] host tests green.
- [ ] single T-Deck build green.
- [ ] ESP32-S3/16MB/DIO verified.
- [ ] merged binary hash/size recorded.
- [ ] two physical T-Decks flashed.
- [ ] A<->B direct messaging passes.
- [ ] fallback ladder passes.
- [ ] V9 recovery path still available.
- [ ] V10 website preview passes.
- [ ] production promotion only after hardware QA.
