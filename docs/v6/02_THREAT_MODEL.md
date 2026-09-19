# V6 Threat Model

## Security promise
V6 aims to protect message content, contact/session secrets and local chat history while minimizing metadata.

V6 MUST NOT claim that radio or internet communication is literally untraceable.
An observer may detect RF activity, timing, IP connections or physical possession of a device.

## Assets
- plaintext messages;
- private identity keys;
- session/ratchet state;
- group key state;
- contact graph;
- local message history;
- Wi-Fi credentials;
- delivery metadata;
- firmware signing trust.

## Adversaries considered

### Passive RF observer
Can record nearby radio traffic.
Goal: should not recover plaintext or stable unnecessary identifiers.

### Untrusted relay node
Can store/forward packets.
Goal: sees opaque envelopes only; cannot read message content.

### Privacy Gateway
Can observe the encrypted tunnel endpoint and timing/volume of tunneled traffic.
Goal: never has message plaintext or messaging private keys. Treat it as a separate metadata observer from the Blind Relay where possible.

### Untrusted internet relay
Can observe connection and opaque mailbox traffic.
Goal: cannot decrypt content and should retain the minimum routing metadata needed for delivery.

### Database compromise
Attacker obtains server database.
Goal: database contains ciphertext, opaque mailbox IDs, expirations and delivery state — not readable chat content.

### Lost/stolen device
Goal: encrypted local data plus device unlock policy reduce offline disclosure risk.
Physical compromise can never be treated as impossible.

### Malformed/malicious peer
Sends corrupt/replayed/oversized/duplicate frames.
Goal: parsers reject safely without memory corruption, unbounded allocation or state exhaustion.

### Firmware tampering
Goal for secure-production profile: only authorized signed firmware boots/updates.

## Explicit non-goals
- hiding the existence of all RF transmissions;
- guaranteeing anonymity against a global traffic-analysis adversary;
- defeating physical laboratory attacks on unlocked hardware;
- promising perfect deletion from wear-levelled flash;
- inventing proprietary crypto as a substitute for reviewed standards.

## Metadata budget
Every field outside encrypted payload needs a written justification.

Default cleartext envelope metadata MAY include only fields required to:
- identify protocol version;
- route toward an opaque destination;
- deduplicate;
- fragment/reassemble;
- expire stale traffic;
- authenticate the protected object as required by the selected crypto design.

Avoid cleartext:
- real/display names;
- phone/email;
- precise location;
- chat title;
- plaintext timestamp unless routing truly requires it;
- plaintext message type when it can be protected;
- long-lived discovery identifier.

## Privacy defaults
- analytics OFF;
- location sharing OFF;
- MQTT/map reporting OFF unless explicitly enabled;
- no plaintext message logging;
- no contact names in production diagnostics;
- no automatic cloud backup;
- no Internet transport while user selected Off-grid.

## Discovery privacy
Design rotating discovery identifiers derived from a scoped secret and epoch.
Requirements:
- verified contacts can recognize permitted peers;
- passive observers should not receive a permanent human-readable identifier;
- rotation must not break message delivery;
- old identifiers expire;
- do not invent the derivation ad hoc: specify and review cryptographically before implementation.

## Padding
Define a small number of payload size classes so exact plaintext length leaks less information.
Padding policy must account for LoRa airtime and regulatory duty-cycle constraints.

## Logging policy
Production logs may record:
- subsystem;
- transport class;
- success/failure code;
- bounded anonymized counters.

Production logs must not contain:
- plaintext;
- private/session keys;
- Wi-Fi passwords;
- full contact identity keys;
- human-readable contact names;
- complete message history.

## Privacy review gate
Every new V6 feature answers:
1. What new data is created?
2. Is it secret?
3. Where is it stored?
4. Who can observe it?
5. How long is it retained?
6. Can it be omitted?
7. Can it be encrypted?
8. Can an identifier rotate?
9. How is it deleted/expired?
10. What happens after device compromise?


## VPN / Privacy Tunnel boundary
A VPN/tunnel reduces metadata visible to the local Wi-Fi/ISP, but shifts some network metadata to the Privacy Gateway. It is not an anonymity guarantee. V6 E2EE remains mandatory even inside the tunnel. If the user configures Tunnel Required, direct Internet fallback is a privacy violation and must fail closed.
