# V27 MASTER PROJECT — FASE 2: TECHNICAL CONTRACTS, EDGE CASES & UX

Status: DESIGN ONLY — no implementation authorized from this branch.

## A. Wi-Fi compatibility contract

ESP32-S3 target:
- 2.4-GHz 802.11 b/g/n
- no native 5-GHz support

Automatic compatibility ladder:

### Profile 1 — Standard
- STA mode
- all-channel scan
- strongest compatible AP
- normal DHCP
- no permanent BSSID pin
- no forced PHY override

### Profile 2 — Modern transition
Target:
- WPA2
- WPA2/WPA3 transition
- WPA3-SAE when supported by SDK/AP
- PMF capable / not blindly forced
- SAE H2E transition support where available

### Profile 3 — Legacy / IoT compatibility
Target:
- older routers/repeaters
- 802.11 b/g/n
- HT20
- conservative PMF
- WPA/WPA2 fallback where safe and supported

WEP is intentionally unsupported.

### Profile 4 — EU/NL rescue
Target:
- channels 1–13
- hidden SSID
- mesh/extender APs
- one-shot BSSID/channel hint
- then release pinning so roaming can resume

After profile 4:
- quiet bounded backoff
- never infinite reconnect storm

## B. Wi-Fi failure classification

V27 must distinguish:
- SSID not found
- auth failure
- association timeout
- PMF/SAE mismatch
- AP leaves / roam
- got link but no DHCP
- got DHCP but no DNS
- Internet unavailable
- relay TLS failure
- relay auth failure

Do not collapse all failures into "Wi-Fi failed".

## C. Internet readiness contract

GLOBAL_READY requires:
1. Wi-Fi associated
2. IP assigned
3. required name resolution works or direct endpoint is available
4. TLS/server authentication succeeds
5. relay authentication succeeds
6. subscriptions/routes ready

Anything less remains RF-capable but not GLOBAL_READY.

## D. Master Gateway contract

Purpose:
- allow a 5-GHz/Ethernet upstream to serve T-Decks over 2.4 GHz.

Gateway must provide:
- 5-GHz Wi-Fi client and/or Ethernet WAN
- 2.4-GHz AP
- DHCP
- DNS forwarding
- NAT/routing
- secure local admin
- automatic reconnect upstream
- stable downstream SSID

Gateway must NOT:
- decrypt V27 chat
- hold device private keys
- become required for RF
- emulate P1 Pro
- bridge Internet traffic into LoRa automatically

Recommended product behavior:
- T-Deck sees one known 2.4-GHz SSID
- user does not choose 5 vs 2.4 GHz on T-Deck
- upstream can change without changing chat identity

## E. Messaging contract

One message object:
- logical message ID
- sender identity
- conversation identity
- timestamp
- payload
- delivery state
- optional transport observations

Transport observations never define separate chat history.

### DM
V27-to-V27:
- pair-wise E2E key
- opaque route
- fixed-size encrypted envelope
- global + RF dedup

V27-to-P1/legacy:
- RF path remains authoritative compatibility path

### #Group
- existing channel secret remains group trust root
- global copy encrypted
- V27 global sender signature verified
- public/well-known channel is not labelled private

## F. Delivery-state contract

Internal:
- CREATED
- RF_ACCEPTED
- GLOBAL_ACCEPTED
- GLOBAL_QUEUED
- DEVICE_DELIVERED
- FAILED

User-facing:
- Sending
- Sent
- Delivered
- Offline / Not sent

Rule:
- "Sent" cannot mean "sitting only in a hidden RAM queue after both actual transports failed".

## G. Queue contract

Device queues:
- bounded count
- bounded byte usage
- TTL
- oldest expendable global mirror may be dropped before RF state
- no unbounded dynamic history

Server queue:
- ciphertext only
- TTL
- per-device quota
- per-route quota
- idempotency by message-id
- delete/mark after authenticated device receipt

## H. Privacy contract

Device private keys:
- local only

Wi-Fi credentials:
- local only

Channel secret:
- local only

Relay sees:
- IP/timing inevitably
- opaque route
- fixed-size ciphertext
- limited delivery metadata

Relay does NOT see:
- plaintext body
- Wi-Fi password
- device private key
- raw channel secret

No claim of network-level anonymity.

## I. TLS / relay contract

Production:
- server-authenticated TLS or equivalent secure transport
- hostname/certificate verification
- no setInsecure()
- no plaintext port fallback
- short-lived/revocable credentials
- no wildcard subscriptions for ordinary devices
- reconnect with jitter/backoff

Development public broker can exist only behind an explicit development build flag and can never ship as Stable.

## J. UX contract

Normal user flow:
1. power on
2. select Wi-Fi once
3. open chat
4. type
5. send

Everything else automatic.

Normal UI hides:
- broker
- TLS
- port
- PMF
- SAE
- BSSID
- RF/global selector
- retry counter
- database provider

Advanced diagnostics may show:
- Wi-Fi phase
- disconnect reason
- IP
- relay health
- RF health
- queue count
- firmware version

## K. Failure injection matrix

Must be designed before code:
- wrong Wi-Fi password
- hidden SSID
- channel 13
- WPA2 router
- WPA2/WPA3 transition AP
- WPA3 AP
- PMF required AP
- old repeater
- mesh AP roaming
- slow DHCP
- DNS failure
- Internet cut while Wi-Fi stays associated
- TLS certificate failure
- relay offline
- revoked credential
- queue full
- duplicate same message via RF+Internet
- reboot mid-reconnect
- P1 V8 active while Internet fails
- Master Gateway upstream loss

## L. Fase 2 exit criteria

No coding until:
- Wi-Fi ladder is finite and deterministic;
- Internet-ready is separated from Wi-Fi-connected;
- 5-GHz bridging is assigned to Master Gateway, not T-Deck firmware;
- all normal-user flows remain zero-config;
- delivery states cannot lie;
- privacy boundaries are explicit;
- failure matrix covers every subsystem boundary.
