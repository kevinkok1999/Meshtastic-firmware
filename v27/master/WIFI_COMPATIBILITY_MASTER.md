# V27 MASTER — Wi-Fi Compatibility Engine

Status: DESIGN ONLY. No implementation is authorized by this document.

## Mission

V27 must connect reliably to as many practical 2.4-GHz Wi-Fi environments as the ESP32-S3 hardware and secure software stack reasonably allow.

The Wi-Fi subsystem is a RELEASE GATE, not a convenience feature.

Hardware truth:
- ESP32-S3 Wi-Fi is 2.4 GHz 802.11 b/g/n.
- 5-GHz reception cannot be added in T-Deck firmware.
- Optional Master Gateway handles 5-GHz/Ethernet upstream and exposes a normal 2.4-GHz network downstream.

## Core principles

1. One saved SSID/password, zero transport tuning for normal users.
2. Modern secure path first.
3. Compatibility broadens only after a failed attempt.
4. One reconnect owner.
5. Never scan over an active association/DHCP attempt.
6. Wi-Fi association, DHCP, Internet and relay health are separate states.
7. No router-specific permanent hacks.
8. AP/BSSID/channel hints are one-shot and releasable for roaming.
9. No automatic open-network joining by default.
10. WEP is not a V27 target.
11. Every retry ladder is bounded and ends in quiet backoff.
12. RF remains usable throughout all Wi-Fi failures.

## Wi-Fi state machine

States:
- RADIO_OFF
- IDLE
- DISCOVERING
- AP_FOUND
- ASSOCIATING
- LINKED_NO_IP
- DHCP_RECOVERY
- LOCAL_NETWORK_READY
- INTERNET_CHECKING
- INTERNET_READY
- RELAY_CONNECTING
- GLOBAL_READY
- BACKOFF
- CAPTIVE_OR_FILTERED
- AUTH_FAILED
- SSID_NOT_FOUND

The UI does not expose these internal states directly except in Diagnostics.

## Discovery engine

V27 discovery should:
- scan all legal 2.4-GHz channels for the regional build;
- include hidden SSID support when the user has already saved/entered the SSID;
- collect SSID, BSSID, channel, RSSI and advertised auth mode;
- collapse duplicate BSSIDs under one human SSID entry;
- prefer a good compatible BSSID rather than blindly the strongest raw RSSI;
- avoid scanning while association or DHCP is in progress;
- delete/free stale scan results after use.

For NL/EU builds, validation must explicitly cover channels 12 and 13.

## Compatibility ladder

### Profile A — Modern Auto

Default first attempt.

Targets:
- WPA2-Personal
- WPA2/WPA3 transition
- WPA3-Personal where SDK support is compiled in
- PMF optional behavior
- strongest compatible AP
- normal 20/40-MHz negotiation
- normal DHCP

No BSSID pinning.

### Profile B — WPA2 conservative

Used when modern auto fails.

Targets:
- WPA2-only routers
- routers with imperfect WPA3 transition behavior
- older ISP routers
- hotspots

Behavior:
- WPA2-compatible association preference
- PMF not forced
- no forced SAE dependency
- no permanent AP pin

### Profile C — Legacy 2.4-GHz compatibility

Used only after A/B fail.

Targets:
- older 802.11b/g/n routers
- old repeaters/extenders
- IoT-oriented AP configurations

Behavior:
- 802.11 b/g/n compatibility
- HT20 conservative bandwidth
- WPA/WPA2 mixed-mode compatibility where supported
- PMF not required
- conservative reconnect timing

WEP remains unsupported.

### Profile D — Mesh / Extender / Hidden / EU rescue

Final bounded attempt.

Targets:
- multiple BSSIDs with same SSID
- mesh nodes
- extenders
- hidden SSIDs
- channels 12/13 in NL/EU
- APs where roaming selection repeatedly chooses a bad BSSID

Behavior:
- targeted saved-SSID scan
- one-shot selected BSSID/channel hint
- channels 1–13 regional policy
- reconnect to best candidate
- release pinning after successful join so later roaming is possible

### Profile E — DHCP recovery

This is NOT another Wi-Fi authentication retry.

If link is established but IP never arrives:
- do not immediately tear down a good Wi-Fi association;
- restart/recover DHCP/network interface first;
- wait a bounded interval;
- only then re-associate if network-layer recovery fails.

This prevents unnecessary WPA renegotiation on routers with slow DHCP.

## Authentication support policy

Primary:
- WPA2-Personal
- WPA3-Personal where SDK support is present
- WPA2/WPA3 transition

Compatibility:
- WPA/WPA2 mixed mode where supported and explicitly selected by the user

Not automatic targets:
- WEP
- anonymous open auto-join
- captive-portal credential bypass

Open network:
- user may explicitly select one if product policy later permits it;
- never silently auto-join unknown open networks.

Enterprise:
- WPA2-Enterprise is a separate future capability because credentials, certificate validation and UX differ from home-router PSK networks.
- It must not be mixed invisibly into the consumer connection ladder.

## WPA3 / PMF rules

- Detect at build time whether WPA3-SAE support exists.
- Never label WPA3 as supported if the SDK was built without it.
- WPA3 requires PMF semantics.
- Use PMF optional for broad station compatibility unless the target AP requires PMF.
- Do not globally force PMF-required because that excludes many WPA2 routers.
- Support WPA2/WPA3 transition mode without requiring user intervention.

## BSSID / mesh rules

For one SSID with several APs:
- rank candidates by compatible security + RSSI + recent success;
- remember recent failed BSSID temporarily;
- avoid repeatedly selecting the same failing node;
- BSSID bans/hints expire automatically;
- successful roaming must not change chat identity.

Do not store permanent router MAC assumptions.

## DHCP / DNS / Internet classification

Wi-Fi connected does NOT equal Internet connected.

After association:
1. confirm IP/DHCP;
2. confirm usable default route;
3. test required name resolution or endpoint reachability;
4. establish verified relay security session;
5. authenticate relay;
6. only then set GLOBAL_READY.

Possible state examples:
- linked but no DHCP;
- LAN works, Internet down;
- Internet works, DNS down;
- Internet works, relay down;
- relay TLS fails;
- relay credential expired.

RF remains independent throughout.

## Captive networks

V27 should detect the likely condition:
- Wi-Fi link and IP exist;
- general Internet/relay unavailable;
- captive portal indicators or redirect behavior observed.

Normal behavior:
- do not repeatedly reconnect to Wi-Fi;
- mark Internet route unavailable;
- retain RF operation;
- show a simple user-facing notice only when relevant.

V27 must not attempt to bypass portal authentication.

## Reconnect ownership

Exactly one firmware component owns Wi-Fi reconnect.

Rules:
- OS/Arduino auto-reconnect and V27 reconnect logic must not fight each other;
- scan worker cannot initiate association;
- UI cannot launch parallel reconnects;
- relay reconnect cannot restart Wi-Fi;
- Wi-Fi retry cannot block LoRa/BLE/UI loops.

## Backoff

Example design:
- four compatibility profiles in foreground;
- then bounded exponential/stepped backoff;
- jitter;
- immediate retry after explicit user action;
- immediate recovery attempt on real Wi-Fi return event;
- never a reconnect storm.

Exact timings are an implementation decision reviewed by Firmware + Wi-Fi + Validation Masters.

## Saved network strategy

V27 should support multiple known networks later without making setup harder:
- home Wi-Fi
- phone hotspot
- trusted Master Gateway
- other explicitly saved networks

Selection score:
- security compatibility
- recent success
- RSSI
- last failure reason
- Internet availability history

Never automatically add networks discovered by scan.

## Diagnostics

Normal user sees:
- Connecting
- Connected
- No Internet
- Offline / RF available

Expert diagnostics may show:
- SSID
- channel
- RSSI
- BSSID
- advertised auth mode
- profile attempted
- disconnect reason
- association duration
- DHCP duration
- IP/DNS status
- relay status
- retry/backoff state

Never log:
- Wi-Fi password
- private chat keys
- channel secrets
- relay credentials

## Router compatibility validation matrix

V27 Stable requires real tests covering at minimum:

### Modern
- current dual-band ISP router with same SSID on 2.4/5 GHz
- split 2.4/5-GHz SSIDs
- WPA2
- WPA2/WPA3 transition
- WPA3-Personal
- PMF-capable AP
- mesh Wi-Fi

### Older
- older 2.4-GHz 802.11b/g/n router
- WPA/WPA2 mixed router if supported by stack
- 20-MHz-only AP
- old repeater/extender
- slow-DHCP router

### Special
- Android hotspot
- iPhone hotspot where available
- hidden SSID
- channel 12
- channel 13
- multiple BSSIDs sharing one SSID
- guest network with Internet
- Wi-Fi associated but WAN unavailable
- DNS failure
- captive network
- Master Gateway downstream AP

For every test record:
- visibility
- chosen AP/BSSID
- association result
- time to association
- time to DHCP
- Internet-ready time
- reconnect after AP restart
- reconnect after WAN restart
- heap minimum
- reset/watchdog count

## Acceptance targets

Stable requires:
- no infinite connecting state;
- no scan/connect race;
- no repeated destructive Wi-Fi reset;
- no false GLOBAL_READY;
- no router-specific permanent BSSID pin;
- successful fallback between compatibility profiles;
- RF chat remains responsive throughout;
- previously proven P1 V8 path unchanged;
- modern and older test classes both pass at agreed release threshold.

## Master review ownership

Primary: Wi-Fi / Connectivity Master

Required reviewers:
- Embedded/Firmware Master
- Security Master
- UX Master
- Validation/Reliability Master
- Master Gateway/Network Master

No Wi-Fi implementation can ship from one engineering branch without these cross-reviews.
