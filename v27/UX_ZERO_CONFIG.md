# MeshOffGridNL V27 — Zero-Config UX Contract

V27 must feel like one messaging product, not a radio tool plus an Internet tool.

## Primary user flow
1. User powers on the T-Deck.
2. User selects/saves Wi-Fi once.
3. User opens an existing contact or #/group channel.
4. User types a message and presses Send.
5. V27 decides transport automatically.
6. The message appears once in the same conversation regardless of transport.
7. Existing channel membership/secret automatically defines the same worldwide private group; there is no second “Internet group” to create.

No MQTT host, broker port, relay topic, certificate, queue, radio mode, routing mode, retry mode, or cryptographic key is required from the normal user.

## Automatic transport policy
- Internet healthy and recipient V27-global capable: global encrypted delivery is route 1; RF is held as route 2/fallback unless legacy/P1/group compatibility requires an RF copy.
- Internet unavailable: send via the existing RF/MeshCore path without user action.
- Internet returns: reconnect global transport automatically in the background.
- Both paths deliver the same logical message: show only one bubble.
- Relay/backend failure must never disable local RF chat.
- Wi-Fi association failure must never disable local RF chat.
- P1 Pro V8 remains available through the existing radio path.
- #/group messages use the same conversation and existing MeshCore channel secret for the global encrypted copy.

## UI rules
Normal chat UI exposes only human concepts:
- Sending
- Sent
- Delivered
- Queued
- Offline / mesh available

Do not expose normal users to:
- MQTT
- broker
- port 1883/8883
- TLS internals
- relay topics
- packet IDs
- LoRa-vs-Internet selectors
- retry counters
- database/backend names

Advanced diagnostics may exist in a clearly separate expert/diagnostics surface, never in the send flow.

## Wi-Fi
- Saved trusted Wi-Fi networks auto-join first.
- If no trusted network can provide Internet, V27 may automatically try an unknown open network only through the dedicated Untrusted Internet sandbox.
- The legacy unsandboxed open-auto-join path remains disabled; opportunistic open Wi-Fi is a separate V27-only policy.
- Unknown open networks never become trusted/saved automatically.
- Losing Wi-Fi silently degrades to mesh.
- Reconnecting Wi-Fi silently restores global transport.
- No message composition state is lost during network transitions.

## Message semantics
One user action creates one logical message.
A message keeps one stable logical ID across transports.
Deduplication is mandatory before UI insertion.
Retries cannot create extra visible bubbles.
Transport changes do not split history into different threads.

## Channel privacy semantics
- Private/custom channels inherit confidentiality from their existing MeshCore channel secret.
- A deliberately public/well-known channel is not a private conversation and must never be labelled private merely because relay transport is encrypted.
- Global channel sender identity is device-signed in V27, but a display name is still a user-chosen label; cryptographic identity and human identity are not the same thing.
- No extra group password is introduced by V27: the existing channel secret remains the trust root.

## Privacy defaults
- Global message payloads are end-to-end encrypted.
- No plaintext message body in relay logs.
- No plaintext message body in cloud database.
- No GPS/location upload as a side effect of global chat.
- No contacts upload unless explicitly required by a future feature and separately consented.
- Opportunistic unknown-open Wi-Fi is allowed only inside the V27 Untrusted Internet sandbox with verified TLS + E2E; the legacy unsandboxed open-auto-join remains disabled.
- Low-level MQTT UI remains hidden by default.

## Failure behavior
If Internet relay fails:
- keep RF chat usable;
- queue only the bounded Internet copy when useful;
- never block the UI;
- never block radio processing;
- recover in the background with bounded backoff.

If RAM pressure occurs:
- drop expendable relay copies before local RF/chat state;
- never use an unbounded queue.

If credentials expire:
- refresh in the background;
- local RF remains usable;
- do not ask the user for broker credentials.

## Recovery
- V26 remains available as rollback.
- P1 Pro V8 is not modified.
- V27 must survive reboot with existing local chat/contact identity intact.
- Global transport provisioning must be recoverable without factory-erasing the radio identity.

## Release gate
V27 is not user-ready unless all of these are demonstrated:
- Wi-Fi connected -> global message without manual transport selection.
- Wi-Fi removed mid-chat -> next message still sends through RF.
- Wi-Fi restored -> global route returns automatically.
- Duplicate Internet+RF arrival -> one bubble.
- Global group self-echo -> no duplicate own-message bubble.
- Add/remove a channel -> global subscriptions self-refresh without manual broker setup.
- Backend offline -> P1 Pro V8 and local RF chat still work.
- Reboot -> saved Wi-Fi auto-joins and global transport resumes.
- No low-level MQTT/broker settings are required in the normal flow.
