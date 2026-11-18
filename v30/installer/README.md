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

## Derive the native development map trust pin

For a full-v30 package and independently trusted publisher key, add
`--print-map-pin` to the signed verifier command. On success stdout is only
the lowercase SHA256 of the logical MMN1 pack (not the padded partition);
failure uses stderr and a nonzero exit. The verifier checks the authenticated
manifest and its artifacts, then rechecks the complete signed map image in
the exact bounded snapshot used for the digest. This is a potential input to
the explicit development native-map compile pin, never a production release
authorization. Do not supply a pin computed from untrusted downloaded data.

## Review exact flash-sector boundaries (no hardware access)

```sh
python v30/installer/flash_review_plan.py manifest.json manifest.sig publisher.pub \
  --public-key-sha256 "$TRUSTED_PUBLIC_KEY_SHA256"
```

Prints a signed full-v30 development package's five write ranges and the
sector-rounded half-open erase extents. Explicitly preserves NVS, app1,
SPIFFS and coredump, refuses all app1 writes even when safe_mode is set, and
does not authorize flashing or erase-all. Requires readback of the actual
device layout, selected compatible board, user approval, backup and physical
tests before any hardware write. Resetting OTA data changes the boot choice;
preserving app1 bytes by itself does not prove working rollback. The output is
review metadata, not an esptool command or a browser release manifest.

## Verify offline pre/post flash snapshots

```sh
python v30/installer/verify_flash_readback.py manifest.json manifest.sig publisher.pub \
  --public-key-sha256 "$TRUSTED_PUBLIC_KEY_SHA256" --before before.bin --after after.bin
```

Only compares two separately acquired, complete 16 MiB local flash snapshots. The pre-flash partition bytes must match the signed layout exactly. The post-write snapshot, acquired before reboot, must contain all expected segment digests, erased sector padding, identical protected regions, and no other changed bytes. Keep snapshots on the owner's local device; they contain private data and must never be uploaded to CI or shared. This tool has no device I/O, cannot validate boot success, and cannot approve installation or release. Hardware encryption and different layouts require a separate validated process.
