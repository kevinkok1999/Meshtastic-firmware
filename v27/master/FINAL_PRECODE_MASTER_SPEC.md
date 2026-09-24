# V27 FINAL PRE-CODE MASTER SPECIFICATION

Status: AUTHORITATIVE DESIGN FREEZE CANDIDATE

This document is the single top-level contract for the next V27 implementation branches.
Detailed subsystem documents remain normative where they do not conflict with this file.

## 1. Immutable boundaries

Never modify:
- P1 Pro V8 firmware/artifacts;
- V26;
- V19;
- any older released firmware or installer artifact.

V27 compatibility is implemented entirely from the V27 side.

## 2. Product behavior

MeshOffGridNL V27 is one worldwide messaging product.

Route order for V27-capable peers:
1. authenticated global Internet route;
2. RF route after bounded global failure/unavailability.

Exceptions:
- P1/legacy peers use their existing RF compatibility route immediately;
- groups with unknown/mixed legacy membership may require compatibility RF behavior;
- local RF remains independently usable with every cloud component offline.

One user send creates one logical message and one visible bubble.

## 3. Connectivity hierarchy

Preferred:
1. saved trusted Wi-Fi;
2. trusted Master Gateway;
3. saved trusted hotspot/network;
4. V27 Opportunistic unknown-open network in Untrusted Internet sandbox;
5. RF-only.

T-Deck ESP32-S3 remains a 2.4-GHz client.
5-GHz/Ethernet upstream belongs to the optional Master Gateway.

## 4. Wi-Fi master behavior

- one reconnect owner;
- no scan/associate races;
- modern secure profile first;
- WPA2/WPA3 transition/WPA3 where compiled;
- legacy WPA/WPA2 fallback only after modern attempts;
- WEP unsupported;
- hidden SSID + regional legal channels;
- mesh/extender BSSID scoring with expiring hints;
- DHCP recovery before destructive reassociation;
- Wi-Fi link != Internet != relay readiness;
- bounded backoff;
- unknown open networks never become trusted automatically;
- OWE preferred when compiled and offered;
- unknown-open Internet path requires verified TLS + E2E and no exposed LAN admin services.

## 5. Global protocol v3 invariants

- 128-bit logical message ID;
- 128-bit opaque route/capability;
- 96-bit AEAD nonce;
- AES-256-GCM or reviewed equivalent AEAD;
- fixed-size padded envelope;
- protocol version + crypto-suite version;
- strict domain separation;
- sender identity, ordering data, exact plaintext length inside ciphertext;
- channel secrets never transmitted;
- DM route derived from pairwise secret/capability;
- group route derived from channel/group secret/capability;
- signed V27 group sender identity;
- unknown mandatory protocol flag -> fail closed;
- durable bounded replay/idempotency window.

RC protocol v2 is not silently redefined.

## 6. Message lifecycle

Internal minimum states:
- CREATED
- ENCRYPTED
- LOCAL_JOURNALED
- GLOBAL_PENDING
- GLOBAL_ACCEPTED
- RF_PENDING
- RF_ACCEPTED
- RECIPIENT_DELIVERED
- EXPIRED
- FAILED

Rules:
- relay acceptance is not recipient delivery;
- hidden RAM queue alone never means Sent;
- crash/reboot resumes safe pending work;
- duplicate RF/Internet/server replay produces one bubble;
- timestamp is display metadata, not sole ordering/security source;
- monotonic/conversation ordering data supplements wall clock.

## 7. Backend/relay

Production relay:
- dedicated/controlled;
- authenticated;
- short-lived/revocable device credential;
- verified TLS or equivalent;
- no anonymous wildcard subscriptions;
- ciphertext-only bounded store-and-forward;
- TTL + quota + idempotency;
- recipient-device receipts;
- rate limits;
- at least two failure domains or equivalent tested failover.

Public broker:
- development/testing only.

Supabase/equivalent:
- control plane, registry, credential/revocation, policies, optional encrypted queue metadata;
- RLS mandatory for exposed data;
- no service-role secret in firmware.

## 8. Abuse and social safety architecture

- unknown senders enter request/quarantine flow;
- block/mute applies across all transports;
- per-device and per-route quotas;
- malformed envelopes rejected before UI insertion;
- no plaintext server moderation dependency required for basic anti-spam;
- abuse controls cannot starve RF/UI tasks.

## 9. RF architecture

Compatibility lane:
- exact existing P1 V8 compatible profile;
- never retuned by global/Wi-Fi logic.

V27 advanced lane:
- V27-to-V27 capability negotiated only;
- RF-health engine;
- CAD-assisted polite transmit decisions;
- bounded temporal diversity;
- interference classification;
- selective repair/FEC only after separate protocol review;
- regional legal-profile whitelist;
- failure returns safely to compatibility/global behavior.

No arbitrary blind hopping.

## 10. Device lifecycle

Before Stable:
- persistent device identity;
- identity recovery/ownership transfer design;
- signed firmware;
- rollback-capable update;
- anti-rollback production policy;
- network reset != ownership reset;
- reset reason/watchdog/brownout diagnostics;
- secrets redacted from logs;
- firmware update never trusted merely because an open WLAN is connected.

## 11. Resource budgets

Every implementation wave must define:
- static + peak heap;
- PSRAM use;
- task stack size;
- queue count and bytes;
- MQTT/relay packet size;
- reconnect CPU budget;
- flash journal wear budget;
- RF loop latency budget;
- UI latency budget.

No Stable with:
- progressive heap loss;
- watchdog reset;
- RF starvation;
- reconnect storm;
- unbounded queue/subscription growth.

## 12. Master Gateway

Optional, never required.

Provides:
- 5-GHz Wi-Fi and/or Ethernet WAN;
- robust 2.4-GHz AP;
- DHCP/DNS/NAT;
- automatic WAN recovery.

Never:
- decrypts V27 chat;
- owns device private keys;
- automatically floods Internet into LoRa;
- becomes required for local RF.

## 13. Release validation

Required before Stable:
- broad modern + legacy 2.4-GHz router matrix;
- unknown open Wi-Fi + invalid-TLS interception test;
- captive portal test;
- two unrelated Internet connections;
- relay region/failure-domain outage;
- P1 V8 bidirectional RF;
- global DM;
- global group;
- mixed Internet/RF duplicate;
- reboot mid-send;
- power loss with queued message;
- credential expiry/revocation;
- queue pressure;
- long-duration Wi-Fi + BLE + LVGL + LoRa soak;
- signed update failure + rollback;
- installer + rollback.

## 14. Implementation rule

Do not code directly on v27-master-design.
Do not merge v27-rc2-work wholesale.

Next implementation branch starts fresh from pinned v27-rc1 and ports approved components according to CODE_AUDIT_PRE_IMPLEMENTATION.md.

## 15. Definition of done

V27 is Fully Functional / Stable only when:
- user can simply open chat and send worldwide whenever usable Internet exists;
- device autonomously finds/reuses connectivity according to policy;
- Internet failure automatically gives way to RF where applicable;
- P1/legacy compatibility remains intact;
- no duplicate messages;
- delivery status is truthful;
- production relay is authenticated and failure tolerant;
- recovery/update paths are safe;
- all release gates pass on real hardware.
