# V30 relay HTTP candidate (local development only)

The exported async createV30RelayService factory in V30RelayHttp.mjs returns a
Node HTTP Server. The embedding program supplies the pinned issuer public key,
storage adapter, and server clock, then calls listen explicitly. There is no
automatic startup, network grant issuer, deployment, or provisioned secret.

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
trusted time, TLS, secure key provisioning, an exclusive process lock or an
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
This is host evidence; no production or public-network claim is made.
