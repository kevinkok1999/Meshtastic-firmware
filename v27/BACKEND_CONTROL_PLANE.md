# MeshOffGridNL V27 — Production Control Plane & Relay Contract

## Goal

The production backend exists to route opaque encrypted envelopes, issue/revoke short-lived device credentials, and optionally retain ciphertext briefly for offline delivery.

It is **not** the source of truth for local chat. RF/MeshCore remains independently usable if every cloud component is offline.

## Zero-config requirement

Normal users never configure:
- MQTT host or port
- relay topic
- API key
- Supabase project
- broker credentials
- route capabilities

After Wi-Fi is saved, provisioning/reconnect/credential refresh happen automatically.

## Privacy boundary

The service may necessarily observe connection IP/timing and opaque routing identifiers. It must not receive:
- plaintext message bodies
- channel secrets
- device private keys
- Wi-Fi passwords
- plaintext contact lists
- GPS/location merely because chat is enabled

V27 must never claim network-level anonymity. The goal is strong content confidentiality plus metadata minimization.

## Production routing

The public development broker is not a production dependency.

Production should use an authenticated relay with:
1. Random per-device relay identity.
2. Short-lived revocable device credential.
3. ACL-bound subscriptions.
4. Pair-wise or opaque capability token per conversation.
5. Fixed-size encrypted V27 envelopes.
6. Server-authenticated secure transport (TLS or equivalent).
7. Bounded publish/subscription rates.

Pair-wise ECDH-derived route capabilities remain useful because possession cannot be derived from a public key alone. A production relay may additionally group capabilities under a random authenticated device inbox to reduce reconnect/subscription cost. That outer inbox must not replace end-to-end encryption.

## Supabase role

Supabase is the proposed control plane, not the device packet crypto layer.

Recommended data categories:
- device registry: device id, owner/auth binding, public identity/fingerprint, revoked_at
- relay credentials: server-side issuance metadata only; never plaintext long-lived device secrets
- route capabilities: opaque identifiers, scoped to an authenticated device
- encrypted envelopes: opaque ciphertext, message id, opaque route, expiry
- delivery receipts: message id, opaque route/device reference, expiry
- abuse counters: bounded/rate-limit state

RLS is mandatory for exposed tables. Policies should use authenticated roles and explicit ownership/membership checks. Service-role/secret keys remain server-side and are never embedded in firmware.

Supabase Realtime, if used by future web/mobile clients, uses private channels with authorization. The T-Deck does not need to implement the entire Supabase Realtime protocol if a smaller authenticated relay is more memory-efficient.

## Offline store-and-forward

Server-side queue rows contain ciphertext only.

Required properties:
- unique idempotency key / message id
- hard TTL
- bounded per-device and per-route counts
- delivery acknowledgement / deletion
- retry-safe replay
- no indefinite chat-history archive by default
- no plaintext search/indexing because the server has no plaintext

## Credential lifecycle

1. Device proves possession of its existing device identity.
2. Control plane issues a short-lived relay credential.
3. Device refreshes before expiry in the background.
4. Revocation immediately prevents new credentials.
5. RF chat continues even if refresh fails.
6. Factory-reset/new identity results in a new relay identity; old credential can be revoked.

## Abuse resistance

Production relay applies:
- per-device connection rate limit
- publish rate limit
- subscribe/capability count limit
- envelope-size exact validation
- TTL/queue quotas
- invalid-auth rejection before routing
- no wildcard subscription for ordinary devices
- backend circuit breakers

Firmware separately retains its own inbound rate guard so a compromised relay cannot starve RF/UI processing.

## Delivery semantics

The UI should distinguish only human-facing states:
- Sending
- Sent
- Delivered
- Queued/offline if necessary

Transport details are diagnostics only.

A server acknowledgement means only “relay accepted ciphertext”.
A delivery receipt means the recipient device accepted/authenticated the envelope.
Neither should be presented as RF acknowledgement unless it actually came from RF.

## Scaling rule

Development can use pair-wise topic subscriptions. Production must be benchmarked at large contact counts and may use authenticated outer inbox multiplexing so reconnect cost does not scale linearly with every historical contact.

Any multiplexing layer must preserve:
- pair-wise E2E message keys
- fixed-size ciphertext
- opaque conversation capability
- no plaintext social graph in message storage

## Supabase project status

No Supabase project is currently connected to this tool session, so this document is a production contract only. Do not embed placeholder project URLs/keys in V27 firmware and do not weaken RF fallback while the backend is unprovisioned.
