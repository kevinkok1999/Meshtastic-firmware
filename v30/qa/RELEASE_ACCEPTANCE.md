# V30 full national release acceptance (2026-11-18)

This is an isolated development candidate for the LilyGO T-Deck Plus EU868,
ESP32-S3 16 MiB flash. It is **not a release authorization**. Earlier firmware
version directories and the website production branch must stay unchanged.

## Reproducible evidence

- Baseline V30 source a673a9ab3c52e93771e45003c617b7f97bb827fb:
  Actions 44314605923 succeeded, including 108 Python tests, real firmware,
  two independent ESP image parsers, partition/offset validation and checksums.
- First complete native-map QA package: Actions 44315324755 succeeded;
  artifact 14432666789. Five real flash images, native map pin compiled into
  the application, no production signature or physical boot test. This initial
  map retained only motorways, places and provincial boundaries; it is superseded
  for release consideration by the balanced national pack below.
- Balanced national map producer 97ff3a4d94d2b9b8397c15f4894d980ca8766dcc:
  Actions 44316158868 succeeded, artifact 14432656677, immutable ZIP digest
  fc2068a4d0b5f10088c0dab18048a9e7252b009c957251ca4ff2040195a0a384.
  All 64,506 selected map features retained, including 42,207 road segments,
  16,596 water features, 5,683 named places and 20 boundary features. Native
  raw partition reader exercised all nine categories in all twelve provinces.
  Logical pack 1,792,835 bytes, SHA256
  b75518fdbf161055dcbe747eba03e793ba7822967762fb8cfa35622b4bf316b6.
  The 4,980,736-byte padded partition needs no SD card. This is sampled and
  simplified open data (2026-11-16), not an exhaustive address/road map or
  satellite layer. See provenance/attribution in the artifact.
- Balanced full candidate source 89e2ad221e25303f1f5da898b6ad719ed1969d75:
  Actions 44316467709 **completed successfully**, artifact 14433690627.
  ZIP SHA256 3ec80d4453ec773bd84c6972d83679a261208ab149df40a864ba17ff20035fd5.
  The workflow validates both exact ZIP digests, signed
  ephemeral QA pin and all 108 province-layer combinations before compiling
  and exporting the complete five-image unsigned candidate. The native reader
  accepted all 64,506 features; app0 is 3,645,616 bytes (417,616 bytes of
  partition headroom), bootloader 15,104 bytes. Both image parsers and all five
  binary hashes plus manifest hash passed. The CI artifact expires 2026-12-02;
  this is not a public release download or evidence of a physical flash.
- Website work is isolated at `kevinkok1999/Meshoffgridnl`, branch
  `v30-website-staging`, commit 056a38603da03baa2ee708a000551c8f2af1bc1c.
  Final website audit Actions 44318292752 passed with 23 V30 and 4 V29
  regression tests, all three V28 relay crypto/join contracts, syntax and API
  function budget checks. The staged executor refuses
  wrong pre-existing partitions, requires exact qualified writes and terminal
  digest readback, and checks protected sectors and erased tails before reboot.
  Unknown or enabled secure boot/flash encryption is a pre-write stop, and
  the browser rejects a signed partition whose internal MD5 is damaged.
  The production website remains unchanged, and public V30 paths remain locked.
  The browser executor is a bench/mock contract, not a qualified device
  serial adapter. Direct use of the old esptool-js read/uncompressed-write
  paths is not approved; see staged protocol evidence.

## Mandatory production gates (not satisfied by CI or simulations)

1. Select compatible spare T-Deck Plus hardware, authorized local backup and
   verified existing layout; qualify the actual transport read-frame digest,
   exact-sector writes, header preservation, complete readback and safe recovery.
   Identify chip revision, flash capacity, secure boot and flash encryption
   read-only; incompatible security configuration is a stop, not a reason to
   change eFuses. Never flash an unreviewed development artifact or erase the
   entire device. Keep device identifiers and readback files private.
2. Verify real boot, restart, power-cut/recovery, preserved NVS/SPIFFS/app1 and
   coredump, and deliberately test rollback behavior (app1 preservation alone
   does not establish it). Test display, touch, keyboard, GPS, map pan/zoom
   and place search offline with no SD, memory and battery under concurrent RF.
   Use at least ten cold boots and ten warm restarts, and a 24-hour soak on a
   spare board; a screen capture alone is insufficient. Back up otadata and
   the tiles partition separately because they are intentionally replaced.
3. On multiple real EU868 devices, test unchanged baseline communication and
   private-channel join, LoRa/ESP-NOW/Wi-Fi transitions, failure recovery,
   collisions and legal airtime; experimental RF remains gated in QA.
   Record delivery and latency across at least 100 messages in each test
   environment. Qualify the actual S21 2.4 GHz hotspot and P1 Pro separately
   before claiming compatibility. Never claim RF parameters beyond measured
   and legally allowed profiles.
4. Qualify browser direct private-channel chat and worldwide relay separately:
   local TLS tests are not a native device or publicly reachable production
   endpoint. Real worldwide delivery, provisioning and reconnect require
   approved HTTPS service and end-to-end test; no claim of deployed worldwide chat.
5. Independently provision the real publisher trust key, verify the exact final
   signed package, immutable HTTPS artifact hosting including map CORS, and
   complete the browser serial hardware qualification. Verify actual release
   links; activate the V30 website only after all gates and approval. Ephemeral
   QA keys cannot satisfy this gate.

No automatic public upload, production signature, or physical write has been
authorized or executed by this workflow. A successful developer build alone
does not establish 100% flashability or emergency reliability.
