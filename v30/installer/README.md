# V30 installer validation (development only)

The unsigned verifier validates board, offsets, sizes, SHA256 and the map's logical CRC and erased padding. The detached verifier additionally validates the exact manifest bytes with Ed25519 against an independently pinned public key. A key and its pin shipped together with an untrusted download are NOT a trust anchor.

```sh
python -m pip install cryptography==50.0.0
python v30/installer/verify_signed_manifest.py manifest.json manifest.sig publisher.pub --public-key-sha256 "$TRUSTED_PUBLIC_KEY_SHA256"
```

`publisher.pub` contains exactly 32 raw Ed25519 public-key bytes; `manifest.sig` contains exactly 64 raw signature bytes covering the unchanged manifest file. The SHA256 pin is lowercase hex of those public-key bytes, supplied from a separately authenticated release trust configuration. No release private key is generated, stored, or requested in this repository. Development tests generate ephemeral keys solely in memory.

The signed verifier rejects ambiguous duplicate JSON keys, invalid signatures, wrong pins, changed artifacts and any manifest claiming production. It reads files only; it neither writes flash nor authorizes release. Production requires a separately managed trust anchor, reviewed release signing procedure, immutable artifact handoff, browser installer integration and all hardware gates.
