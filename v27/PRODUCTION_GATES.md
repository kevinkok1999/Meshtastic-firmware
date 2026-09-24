# MeshOffGridNL V27 — Production Release Gates

V27 is a worldwide messaging product layered on the existing off-grid mesh. A successful compile is necessary but not sufficient for Stable.

## User-experience gates
- One chat timeline for RF and Internet.
- No normal-user MQTT/broker/port/topic/key configuration.
- Saved Wi-Fi reconnects automatically.
- Wi-Fi loss during an open chat requires no user action.
- Global reconnect happens in the background.
- DM and #/group messages deduplicate across Internet and RF.
- Own global group publish cannot create a second bubble.
- P1 Pro V8 remains usable whenever RF is available.

## Privacy/security gates
- Message content remains end-to-end encrypted before relay.
- Exact DM timestamp, full sender identity and exact message length remain inside authenticated ciphertext.
- Global group topic is derived with HMAC from the existing channel secret.
- Channel secret is never transmitted to the relay.
- Global envelopes are fixed-size.
- No chat payloads, private keys, Wi-Fi passwords or channel secrets in logs.
- Open Wi-Fi auto-join remains off by default.
- No location upload as a side effect of messaging.
- Production relay must use authenticated device access; anonymous public-broker access is development-only.
- Production transport must provide server authentication/TLS or an equivalent verified secure transport in addition to E2E payload encryption.
- Device credentials must be revocable and short-lived where possible.
- Backend stores ciphertext only for offline delivery.
- Store-and-forward data has a TTL and bounded retention.

## Reliability gates
- RF works with the Internet/backend fully unavailable.
- P1 Pro V8 bidirectional hardware test passes with V27.
- V27-to-V27 global DM succeeds across two unrelated Internet connections.
- V27-to-V27 global #/channel succeeds across two unrelated Internet connections.
- Internet+RF duplicate arrival displays once.
- Wi-Fi disconnect/reconnect loop does not freeze UI or mesh loop.
- Relay outage does not create reconnect storms.
- Queue pressure never blocks RF.
- Removed channel stops receiving its global subscription after automatic refresh/reconnect.
- Reboot preserves local identity, contacts, channels and normal RF operation.

## Resource gates
- T-Deck/T-Deck Plus build succeeds.
- No unbounded queues or dynamic message-history growth introduced by V27.
- Global relay buffers remain bounded.
- MQTT wire envelope stays within configured client buffer.
- Long-duration Wi-Fi + BLE + LVGL + LoRa soak test passes without watchdog reset.
- Heap/PSRAM watermarks remain acceptable during reconnect and burst messaging.

## Abuse-resistance gates
- Only known DM contacts can be accepted through the global DM path.
- Only devices with the channel secret can derive/decrypt a global channel route.
- Malformed/authentication-failed envelopes are dropped before UI insertion.
- Rate limiting/backpressure must protect the device from Internet message floods.
- Blocking/muting remains effective regardless of transport.

## Production backend gates
- Separate production relay identity from development/test relay.
- Supabase control plane (or equivalent) has RLS and security-advisor checks clean.
- Device registry, credential revocation and encrypted offline-envelope TTL cleanup are operational.
- Backend has no service-role/API secret embedded in firmware.
- Backend outage is tested explicitly.

## Release sequence
1. Dev build
2. Contract + compile CI
3. Two-device bench test
4. P1 Pro V8 interoperability test
5. Different-network global test
6. Wi-Fi failure/recovery test
7. Long-duration soak
8. Limited canary
9. Stable website installer

V26 remains available as rollback and is never overwritten.
