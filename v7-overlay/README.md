# V7 implementation overlay

This directory is intentionally small. The V7 CI checks out the exact upstream
Saitama v1.3.0 commit and exact MeshCore submodule revision documented in
`v7/UPSTREAM.lock.json`, then applies this overlay.

Implemented in the first V7 coding pass:
- ESP-NOW Long Range on ESP32-S3, Wi-Fi channel 1.
- signed capability HELLO frames bound to the sender Wi-Fi MAC.
- verified V7 peer table with expiry.
- ECDH (MeshCore Ed25519/X25519 key exchange primitive) + MeshCore AES/HMAC
  application protection for direct-message payloads.
- application-level ACK, bounded retry, and LoRa fallback.
- cross-transport duplicate suppression.
- normal Saitama/MeshCore LoRa remains the default path for non-V7 peers.
- channels continue to use normal MeshCore/LoRa in this release.

No V1-V6 source, branch or artifact is modified by this overlay.
