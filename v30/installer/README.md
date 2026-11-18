# V30 installer validation (development only)

The unsigned verifier validates board, offsets, sizes, SHA256 and the map's logical CRC and erased padding. The detached verifier additionally validates the exact manifest bytes with Ed25519 against an independently pinned public key. A key and its pin shipped together with an untrusted download are NOT a trust anchor.

```sh
python -m pip install cryptography==50.0.0
python v30/installer/verify_signed_manifest.py manifest.json manifest.sig publisher.pub --public-key-sha256 "$TRUSTED_PUBLIC_KEY_SHA256"
```

`publisher.pub` contains exactly 32 raw Ed25519 public-key bytes; `manifest.sig` contains exactly 64 raw signature bytes covering the unchanged manifest file. The SHA256 pin is lowercase hex of those public-key bytes, supplied from a separately authenticated release trust configuration. No release private key is generated, stored, or requested in this repository. Development tests generate ephemeral keys solely in memory.

The signed verifier rejects ambiguous duplicate JSON keys, invalid signatures, wrong pins, changed artifacts and any manifest claiming production. It reads files only; it neither writes flash nor authorizes release. Production requires a separately managed trust anchor, reviewed release signing procedure, immutable artifact handoff, browser installer integration and all hardware gates.

The installer also compares the exact generated ESP32 partition table against the pinned seven-entry T-Deck Plus layout, with table-format MD5 and erased padding. Both unsigned and signed verification reject any moved, missing or extra partition, including a moved map or SPIFFS user region. The embedded CI additionally verifies PlatformIO's actual generated binary; the MD5 is only the format checksum, not a signature.

## Assemble an unsigned development candidate

Given local files from the exact pinned firmware build and a logical MMN1 pack:

```sh
python v30/installer/build_dev_package.py --bootloader bootloader.bin --partition partitions.bin \
  --ota-data boot_app0.bin --app firmware.bin --map map.meshmap --output dev-candidate
```

The output directory must not exist. The builder copies bounded inputs, pads the raw map to exactly 0x4C0000 with 0xFF, creates a deterministic manifest, and runs the existing validator before creating the output. It never writes hardware, includes no user-data partition, does not distribute a key, and cannot sign or mark a release as production. The manifest is published last; no crash/durability guarantee is implied. The actual input firmware must come from the verified pinned build; a structural manifest does not establish firmware identity or real map coverage. A separate trusted signature and hardware evidence are required.
