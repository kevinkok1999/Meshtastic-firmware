# V30 relay HTTP candidate (local development only)

The exported async createV30RelayService factory in V30RelayHttp.mjs returns a
Node HTTP or HTTPS server. The embedding program supplies the pinned issuer
public key, storage adapter, and server clock, then calls listen explicitly.
Omitting `tls` preserves the local development HTTP server. Supplying
`tls: {key, cert}` (nonempty PEM strings or Buffers, certificate chain
included as needed) creates only an HTTPS server, with TLS 1.2 minimum and a
bounded handshake timeout. Invalid TLS configuration or credentials fail
startup; there is no plaintext fallback or second listener. No caller option
can override the minimum protocol version. Clients must verify the expected
server hostname and its trusted CA. Never disable certificate verification.
There is no automatic startup, public deployment, or provisioned secret.
The optional registered-channel grant issuer is disabled unless explicitly
configured; see `NETWORK_GRANT_CONTRACT.md`.

POST /v30/push accepts only envelope, authorizationGrant, deviceSignature.
POST /v30/poll accepts only scopeId, consumerOriginId, limit, cursor,
authorizationGrant, deviceSignature. Requests must be application/json,
at most 16384 bytes, and complete within the 5-second server timeout.
At most 16 requests parse or wait at once. No plaintext/private-key fields
are accepted. The service verifies the issuer grant plus device proof over
the canonical operation; scope/origin/right/expiry and nonce checks remain
in the existing authority. Grant provisioning is server-side and out-of-band.

V30FileStorage is a single-process candidate implementing load/save with atomic
rename and file/directory fsync, persisting ciphertext entries, sequence, and
consumed grant nonces before success. Persistent state is validated on startup.
A storage failure returns 503 and locks that service instance closed.
The issuer issuance nonce cache is not persisted here. A deployment must supply
trusted time, a valid server identity and CA trust, secure key provisioning, an exclusive process lock or an
atomic multi-writer database, external rate limits, and durability fault tests.
The core's public fields and authorization object API remain internal only.

The server validates opaque transport shape, not encryption. Idempotency lasts
only while the corresponding retained record exists; expiry permits replay as
a new record with a fresh valid grant. Grant issuance and replay tables require
a durable distributed implementation before multi-instance deployment.

Run: node v30/relay/test_v30_http.mjs
The test opens real loopback TCP sockets, uses two fresh Ed25519 client keys,
client-side AES-GCM encryption/decryption, server restart, persisted replay,
oversized input, wrong-key proof, plaintext-field rejection, and failed storage.
Run the complete suite with `node --test v30/relay/test_v30_*.mjs`.
The HTTPS test creates an ephemeral test CA and server certificate with OpenSSL,
verifies CA and hostname, exchanges a client-side AES-GCM encrypted message
between two independent Ed25519 identities on the same channel using actual
TLS 1.2 HTTP requests, and rejects untrusted/wrong-host certificates, older
TLS and plaintext on the TLS endpoint. It checks persisted ciphertext without
the channel secret or plaintext and rejects malformed TLS configuration.
Only loopback host evidence is established; no native client integration,
public network availability, production provisioning or distributed persistence
claim is made.

