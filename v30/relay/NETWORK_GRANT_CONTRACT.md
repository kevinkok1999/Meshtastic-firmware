# V30 private-channel network grant candidate

This is a development contract, not a public service. The relay still exposes only
push/poll unless an operator explicitly constructs it with a matching Ed25519
issuer key and a **trusted, pre-registered** map of opaque scope IDs to raw
Ed25519 membership public keys. Registration/rotation is not a network endpoint.
There is no automatic first-writer scope claiming. V28 and production site
code are not modified.

## Compatibility and keys

The reference Node client `V30ChannelCredential` takes the existing 32-hex
private-channel secret (trim, case-insensitive hexadecimal) and a device-local
Ed25519 private key. Generate a high-entropy random 128-bit channel secret;
this does **not** turn a human-selected weak key into a secure one.

All inputs below are bytes; domain strings are exact ASCII, no terminator.

* Existing native scope: first 16 bytes of `HMAC-SHA256(channelKey, "MOG27-CH-INBOX")`, lowercase hexadecimal. Unchanged legacy scope.
* Membership Ed25519 seed: `HMAC-SHA256(channelKey, "MOG30-CH-MEMBER-ED25519")` (32 bytes).
* Membership verifier: raw 32-byte Ed25519 public key, lowercase hexadecimal, derived locally from the seed.
* Device origin: raw 32-byte Ed25519 device public key, lowercase hexadecimal.

`publicBinding()` returns only `{scopeId,membershipPublicKey}`. An authorized
operator or future approved onboarding flow can register that public binding.
It must verify that the binding belongs to the intended scope before accepting
it. A client that later possesses the same exact secret obtains grants without
learning the service issuer key or a new channel key. A client with a different
key cannot obtain grants for the registered scope even if its display label is
identical. Existing scope and membership verifier are still potential
offline-guessing oracles for weak secrets: random secrets are mandatory.

## Protocol

POST JSON, exact keys, no query parameters, no channel secret. Signatures are
Ed25519 over the exact ASCII bytes, canonical base64url without padding.

1. `/v30/grants/challenge`: `{scopeId,originId,right,clientNonce,deviceSignature,membershipSignature}`.
   `right` is `push` or `poll`; `clientNonce` is a random 16-byte lowercase
   hex value. Both signatures cover `V30-G4-BEGIN-V1|scopeId|originId|right|clientNonce`.
   The server verifies both signatures against the exact registered scope and
   raw device key before reserving capacity.
2. Response: `{scopeId,originId,right,nonce,issuedAt,expiresAt,challengeExpiresAt}`.
   Server-generated nonce is 16 random bytes as lowercase hex. Server UTC
   milliseconds determine `challengeExpiresAt=issuedAt+15000` and
   `expiresAt=issuedAt+60000`. Check exact returned scope, origin, right and
   fields before signing; the reference client does this.
3. `/v30/grants/issue`: `{scopeId,originId,right,nonce,expiresAt,deviceSignature,membershipSignature}`.
   Both signatures now cover `V30-G4-ISSUE-V1|scopeId|originId|right|expiresAt|nonce`.
   The server checks its own pending challenge, exact fields and clock, then
   consumes the challenge before issuing the existing one-operation signed
   grant. Invalid proofs never produce a grant; challenge expiry/restart
   destroys outstanding challenges. No client clock is accepted for issuance.
4. Use the issued grant with the existing signed `/v30/push` or `/v30/poll`
   request. The relay persists consumed grant nonce and ciphertext, not keys or
   plaintext, and rejects grant replay even after restart. An accepted push is
   not a recipient delivery acknowledgement.

The test-only payload uses AES-GCM with a separated local key and origin/scope/
message AAD. It demonstrates ciphertext roundtrip over the relay, **not** the
production V29/V30 RF envelope or firmware encryption integration. These must
be unified before release.

## Limits and gates

* Default issuer limits: 256 outstanding globally, 16 per scope, one per
  scope/origin/right. A server handles at most 16 concurrent requests, the
  inherited body bound is 16384 bytes and read deadline 5 s. No exposed grant
  route by default, no permission from scopeId alone. Registry maximum 4096.
* The single process candidate has no shared database, persistent registry,
  production abuse limiter, issuer key rotation/revocation or multi-instance
  challenge coordinator. An authenticated malicious member can still attempt
  resource exhaustion. Captured begin proofs can reserve new challenge slots
  (not mint grants) after expiry; verified HTTPS is necessary. Production
  requires IP/device/scope limits, trusted provisioning, genuine TLS server
  validation, and bounded retry/backoff.
* Do not use plaintext HTTP for deployed relay traffic. The actual network
  tests use localhost only, no TLS/external network claims. The module does
  not provision credentials, deploy anything or expose private key material
  to the network. JS cannot guarantee all runtime copies are securely erased.
* Native T-Deck/BLE/browser integration, exact production inner envelope,
  durable client queue, compatible V28 bridge migration, trusted registration,
  recipient acknowledgement and independent physical-network/HIL verification
  remain open.

Development verification: `node --test v30/relay/test*.mjs`. Tests exercise the
reference client and both signed HTTP routes using independently generated
device keys, same legacy scope golden vector, encrypted two-device roundtrip,
restart, expired/tampered/mismatched challenge, wrong membership/device,
per-scope/global bounds, default-disabled routes, and original relay tests.
