# MeshOffGridNL V28 — Unified Master Specification

Status: ACTIVE V28 IMPLEMENTATION CONTRACT

V28 starts from the frozen V27 RC1 release commit. V27 and all earlier releases remain immutable.

## 1. Product goal

V28 is one worldwide communications platform with a consumer-grade user experience.

Primary behavior:
- RF/LoRa is route 1 for V28 messaging and is always attempted first.
- If Wi-Fi/global connectivity is available, the encrypted Internet path is route 2 after the RF attempt.
- Both routes carry the same logical message identity so RF + Internet delivery never creates two visible chat bubbles.
- P1/legacy continues to use its existing RF compatibility path without modification.
- The user sees one chat history and never manually chooses a transport.
- Privacy must cooperate with functionality. A failed privacy/global path may reject that path, but must never disable Wi-Fi association, local UI, RF fallback, or legacy/P1 compatibility.

## 2. Immutable foundations

V28 must not modify:
- V27 release artifacts or V27 online installer entry;
- P1 Pro V8;
- V26;
- V19;
- any earlier release artifact.

V28 may inherit proven behavior but every V28-only change is version-gated.

## 3. Professional T-Deck UX

Goal:
Make the device feel familiar, professional, trustworthy and simple instead of engineer-first.

Rules:
- native chat behavior remains the existing chat core;
- no second DM engine;
- no second channel engine;
- no duplicate chat history;
- no alternate RF send path.

Home priorities:
1. Chats
2. Contacts
3. Apps
4. Settings / Quick settings

Advanced tools:
- terminal;
- file manager;
- control/diagnostics;
- RF engineering tools;
- developer functions.

These remain available but are moved away from the primary consumer flow.

## 4. No-dead-ends rule

Every visible button, menu, folder, card or CTA must:
- open a real existing destination;
- perform a real action;
- or display a clear empty/error state with a working next action.

Forbidden:
- empty placeholder pages;
- buttons with no callback;
- folders/cards that lead nowhere;
- modals without a working close/back path;
- hidden pages that trap keyboard/touch focus.

Examples:
- empty Chats -> Start a chat / Create or join #channel;
- empty Contacts -> Add / Discover contacts;
- empty Apps -> Explain no apps + Browse/Back;
- unavailable hardware function -> explain unavailable and provide Back;
- failed Internet action -> continue via RF without blocking the user;
- failed RF attempt -> Internet route may still deliver when Wi-Fi/global connectivity is available.

## 5. Browser / Companion chat shell

Only the browser presentation is redesigned.

Existing browser WebSocket chat protocol remains the real message path.

Browser goal:
- opens directly into Chats;
- professional messenger layout;
- obvious New chat;
- obvious Join #;
- search;
- clear connection state;
- responsive on phone/desktop;
- no technical broker/topic/key controls for normal users.

Existing chat commands and send/receive semantics remain intact.

## 6. #Channels

Existing #channel behavior remains the actual channel messaging path after join.

Public:
- join by hashtag;
- clearly marked Public.

Private:
- simple 8-character V28 join code;
- user never types the actual 128-bit channel secret.

## 7. V28 8-character private join codes

Human format:
- 8 characters;
- display as XXXX-XXXX;
- alphabet excludes ambiguous characters.

Security rule:
- code is never the encryption key;
- code is only an invite locator;
- server stores only hash + metadata;
- server never receives the real channel secret.

Simple default flow:
1. Owner creates private channel.
2. V28 automatically creates a short invite code.
3. Joiner enters code.
4. V28 verifies/rate-limits request.
5. Owner device auto-processes the secure join when policy allows.
6. Real channel secret travels end-to-end encrypted to the joiner.
7. Channel appears in the normal existing channel list.
8. Future messages use the unchanged existing channel path.

Manual approval is an optional stricter setting, not the normal default UX.

## 8. Production global relay

The production global relay is V28 route 2. It never replaces RF as the first send attempt.

V28 Stable must not depend on the public development MQTT broker.

Target architecture:
- MeshOffGridNL-controlled HTTPS relay;
- Vercel Functions + existing Neon PostgreSQL infrastructure;
- ciphertext-only store-and-forward;
- signed Ed25519 device requests;
- replay nonces;
- request quotas/rate limits;
- message idempotency;
- delivery acknowledgements;
- TTL cleanup;
- no chat plaintext;
- no channel secret;
- no Wi-Fi passwords;
- no private keys on server.

## 9. Messaging semantics

- 128-bit logical message IDs;
- 128-bit opaque conversation capabilities;
- fixed-size E2E encrypted envelopes;
- relay accepted != recipient delivered;
- crash-safe local message journal;
- durable replay/idempotency window;
- no visible duplicate when RF/global copies meet;
- wall clock is not the sole ordering/security source.

## 10. Wi-Fi worldwide compatibility

V28 retains and extends the V27 broad 2.4-GHz compatibility work.

Target:
- modern WPA2;
- WPA2/WPA3 transition;
- WPA3 when supported;
- older WPA/WPA2 mixed fallback where safe;
- HT20 compatibility;
- mesh/extenders;
- hidden networks;
- region-legal channels;
- Android/iPhone hotspots;
- slow DHCP;
- DNS/WAN/relay failures separated from association failures.

One reconnect owner.

## 11. Opportunistic unknown open Wi-Fi

If no trusted saved network works, V28 may try unknown open Wi-Fi automatically with minimum user interaction.

This runs only inside an Untrusted Internet sandbox:
- verified TLS mandatory;
- E2E mandatory;
- no plaintext fallback;
- no local admin exposure;
- no firmware trust merely because WLAN is connected;
- no automatic promotion to trusted network;
- captive/invalid-TLS APs are suppressed;
- trusted networks always outrank opportunistic networks;
- RF remains independent.

OWE/Enhanced Open is preferred when supported.

## 12. Connectivity state separation

Never treat these as the same state:
- AP found;
- associated;
- DHCP/IP ready;
- DNS/default route ready;
- Internet reachable;
- TLS valid;
- relay authenticated;
- GLOBAL_READY.

A relay failure must never trigger destructive Wi-Fi loops.

## 13. RF architecture

Compatibility lane:
- P1 Pro V8-compatible RF remains unchanged.

V28 advanced lane:
- V28-to-V28 only;
- capability negotiated;
- RF health scoring;
- SNR/RSSI/noise-floor/retry/valid-packet history;
- CAD-assisted polite transmit behavior where supported;
- bounded temporal diversity;
- interference classification;
- selective repair/FEC only after separate validation;
- no arbitrary blind hopping;
- legal regional profiles only.

## 14. Master Gateway

Optional:
- 5-GHz Wi-Fi and/or Ethernet upstream;
- robust 2.4-GHz downstream AP;
- DHCP/DNS/NAT;
- automatic WAN recovery.

Never:
- decrypt chat;
- own user private keys;
- become mandatory for RF use.

## 15. Privacy/functionality contract

Privacy protects content and identity material without breaking basic operation.

If global crypto/TLS/auth fails:
- reject unsafe global path;
- keep RF usable;
- keep local UI usable;
- keep Wi-Fi association state independent;
- show truthful status;
- do not report false delivery.

No anonymity claims.

## 16. Secure lifecycle

Before Stable:
- signed firmware;
- rollback-capable update;
- anti-rollback production policy;
- network reset separate from ownership reset;
- identity recovery/transfer design;
- reset/watchdog/brownout diagnostics;
- secrets redacted from logs.

## 17. Abuse protection

- unknown sender requests/quarantine;
- block/mute across transports;
- per-device rate limits;
- per-invite rate limits;
- invite expiry/revocation;
- no server plaintext moderation dependency required for baseline spam control.

## 18. Specialist review lines

V28 is reviewed through independent workstreams:
1. Embedded/Firmware
2. Navigation/Information Architecture
3. T-Deck UI/UX
4. Browser UX
5. Wi-Fi/Connectivity
6. RF Engineering
7. Security/Privacy
8. Relay/Backend/Data
9. Reliability/Performance
10. Installer/Release/Operations
11. Independent Functional QA

A workstream may block release.

## 19. Required functional QA

Every release candidate must validate:
- every Home CTA;
- every Apps launcher;
- every Settings category;
- every modal close/back path;
- browser New chat;
- browser Join #;
- private 8-character invite;
- public hashtag join;
- existing DM send/receive;
- existing #channel send/receive;
- RF-first direct-message send order;
- RF-first #channel send order;
- Internet route only after/alongside the completed RF attempt when Wi-Fi/global is available;
- RF/global duplicate suppression;
- no duplicate message;
- Wi-Fi reconnect;
- open-Wi-Fi sandbox failure;
- relay outage;
- reboot mid-send;
- queue pressure;
- P1 V8 compatibility;
- T-Deck build;
- installer download/hash;
- Vercel relay health.

## 20. Stable definition

V28 becomes Stable only when:
- professional UX is complete and no-dead-end audit passes;
- existing native chat semantics remain intact;
- browser shell is functional;
- join-code flow is functional;
- production relay is functional;
- broad Wi-Fi matrix is validated;
- privacy/functionality gates pass;
- RF/P1 compatibility passes;
- real hardware and soak/chaos tests pass;
- installer/release artifacts are verified.

Until then, use Dev/RC labels honestly.
