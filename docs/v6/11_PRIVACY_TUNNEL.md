# V6 Privacy Tunnel / Internal VPN — Architecture Draft

## Purpose
The Privacy Tunnel is an OPTIONAL outer network-privacy layer for Internet and Smart modes.

It does NOT replace V6 end-to-end encryption.

Layering:

Message plaintext
-> V6 E2EE
-> opaque V6 envelope
-> TLS application connection
-> optional Privacy Tunnel (VPN)
-> Wi-Fi / ISP / Internet
-> Privacy Gateway
-> Blind Relay
-> destination

The message remains end-to-end encrypted even if the tunnel is disabled or the tunnel gateway is compromised.

## What the tunnel improves
A correctly configured tunnel can reduce what the local Wi-Fi operator / access network / ISP can directly see about V6 destinations.

Without tunnel:
local network may observe connection toward the V6 relay endpoint.

With tunnel:
local network sees an encrypted tunnel toward the selected Privacy Gateway.
The gateway then forwards allowed V6 Internet traffic.

## What the tunnel cannot promise
Do not market this as "untraceable".

The Privacy Gateway can observe:
- source Internet connection/IP at its side;
- connection timing;
- tunnel byte volumes;
- gateway account/device credential if one exists.

The Blind Relay may still observe:
- gateway/exit IP;
- arrival timing;
- padded/unpadded ciphertext sizes.

A sufficiently capable traffic observer may correlate timing/volume.
E2EE protects message content independently.

## Preferred protocol research
WireGuard-style Layer-3 VPN is the primary research target because:
- compact protocol;
- modern cryptographic design;
- simple peer/public-key model;
- can route all permitted traffic to one peer.

Do not implement a new proprietary VPN protocol.

## ESP32-S3 feasibility
There are current ESP-IDF/lwIP WireGuard components, but they are third-party and MUST be benchmarked/audited before production use.

V6 SHALL NOT assume that a full VPN is free in RAM, flash, power or latency.

Benchmark:
- idle RAM
- handshake peak RAM
- flash size
- reconnect time
- keepalive energy
- throughput
- Wi-Fi coexistence
- latency
- failure behavior
- long-duration stability

## Two implementation tiers

### Tier A — Application Privacy Tunnel (recommended V6.0 path)
Only V6 Internet traffic is sent through a dedicated secure gateway connection.

Advantages:
- less firmware complexity;
- easier policy enforcement;
- does not need to become a general device IP router;
- lower attack surface;
- clear separation from Wi-Fi management traffic.

This can use a secure tunnel transport conceptually similar to a VPN while remaining application-scoped.

### Tier B — Full IP VPN
A WireGuard/lwIP-style interface routes selected/all Internet IP traffic through a gateway.

Only ship if:
- component passes code/security review;
- resource budget fits;
- DNS behavior is controlled;
- failure mode is fail-closed;
- reconnect and sleep behavior are proven;
- no traffic leaks outside the tunnel while Privacy Tunnel is required.

## Fail-closed policy
User setting:
Privacy Tunnel = Required / Preferred / Off

REQUIRED:
- Internet messages are NOT sent if tunnel is unavailable.
- UI says "Privacy Tunnel niet beschikbaar — bericht blijft opgeslagen."
- Never silently bypass to direct Internet.

PREFERRED:
- user explicitly allows direct E2EE Internet as fallback.
- UI indicates whether current message used tunnel or direct Internet.

OFF:
- V6 E2EE + TLS still protects content/application connection.
- no VPN tunnel is used.

Recommended default for the privacy-focused secure profile: REQUIRED after a gateway has been provisioned.
Recommended onboarding default before provisioning: OFF with a clear setup option, not a broken Internet mode.

## DNS
Avoid privacy leaks through ordinary local DNS when the tunnel is REQUIRED.

Requirements:
- relay/gateway endpoint resolution strategy defined;
- once tunnel is up, downstream service resolution occurs inside or through the protected path where practical;
- do not silently query local DNS for V6 service names after fail-closed tunnel activation.

Bootstrap endpoint discovery is a special case and must be documented in threat model.

## Gateway architecture
Privacy Gateway should be separate from Blind Relay where possible.

Reason:
If the same operator/service sees both:
- incoming source connection
- final mailbox activity

it has more metadata correlation power.

Preferred architecture:

T-Deck
  -> Privacy Gateway A
      -> Blind Relay B

Separation can be organizational or at minimum service/logging separation.

## Multi-gateway future
Support a bounded list of gateways:
- user/self-hosted
- MeshOffGridNL-operated
- organization-operated

Do not automatically hop across random third-party infrastructure.

Gateway selection can be:
- fixed
- manually chosen
- privacy profile policy

Do not promise anonymity merely because multiple gateways exist.

## Tunnel identity
Tunnel credentials must be separate from:
- messaging identity
- contact identity
- firmware signing identity
- storage keys

Avoid using the long-term public chat identity as the VPN identity.

## Key storage
Tunnel private keys follow the same secure local-storage rules as other long-term secrets.
Never log them.
Never place production private keys in GitHub or the blind relay database.

## Logs
Gateway production logs should minimize:
- source retention
- destination/application detail
- per-device histories

Operational logs must not contain message plaintext (which should be impossible by design).

Retention policy must be documented and tested.

## UX
Normal screen:

Privacy Tunnel
[ Required ]
"Internetberichten gaan alleen via de beveiligde privacyroute."

Status:
🟢 Tunnel actief
or
🟠 Tunnel niet beschikbaar — berichten wachten

Advanced:
- gateway
- latency
- last handshake
- tunnel IP
- reconnect
- diagnostics

Do not expose WireGuard key material in normal UI.

## Interaction with V6 modes

OFF_GRID:
Privacy Tunnel not used; no Internet messaging.

INTERNET:
If tunnel Required -> InternetTransport only becomes AVAILABLE after tunnel health is good.

SMART:
The privacy requirement remains a hard policy constraint.
Smart routing may choose transports only inside user-approved policy.
It may not bypass a REQUIRED tunnel through direct Internet.

## Routing integration
TransportPolicy receives a NetworkPrivacyPolicy.

Example:
networkPrivacy = TUNNEL_REQUIRED

InternetTransport.available() returns false unless PrivacyTunnel.health == READY.

Messages then remain queued or may use an allowed OFF_GRID path only if the user selected a mode that explicitly permits that path.

## Leak tests
Automated tests MUST prove:
- no relay connection before tunnel ready when REQUIRED;
- no direct fallback after tunnel failure;
- no local DNS request for downstream V6 services after protected routing is active;
- reconnect does not leak direct packets;
- boot sequence does not briefly connect direct;
- time sync/update/telemetry paths are separately classified and do not accidentally bypass policy;
- firmware updates obey their own explicit network policy.

## Kill switch
Implement policy as a network permission boundary, not a UI toggle only.

When REQUIRED:
- default deny for V6 Internet egress;
- allow tunnel bootstrap endpoint;
- allow traffic over established tunnel;
- deny direct relay endpoint.

A UI bug must not be enough to bypass the kill switch.

## Open decisions before coding
- Tier A application tunnel vs Tier B full WireGuard for V6.0
- gateway protocol/component
- gateway provisioning
- bootstrap DNS/IP design
- key rotation
- keepalive policy
- self-hosting
- gateway/relay separation
- IPv4/IPv6 behavior
- captive portal behavior
- Wi-Fi sleep behavior
- resource budget
