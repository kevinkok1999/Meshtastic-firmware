# MeshMap Nano pack builder (M1)

This directory contains the deterministic, offline preprocessing step for V30. It consumes a **normalized JSONL** extract produced from a licensed OSM Geofabrik PBF or an eligible PDOK bulk dataset. It does not fetch tiles and it does not ship production geography in this repository.

## Input contract

Each JSONL line is one feature:

`{"id":"way/1","kind":"primary","province":"Utrecht","name":"N225","zoom_min":8,"geometry":[[5.1,52.1],[5.2,52.2]]}`

Coordinates are longitude/latitude in WGS84 degrees. `province` must be one of the 12 provinces in `nl_profile.json`; every province must occur at least once. Use stable source IDs and preserve the source's data date in the build invocation/provenance. The extractor must normalize names and geometry before invoking the builder.

## Build

```sh
python v30/maps/build_nl.py input.jsonl v30/maps/nl_profile.json out.meshmap
```

The command writes a binary pack and a JSON report beside it (`out.meshmap.report.json`). The pack is deterministic: records are sorted by stable ID, names are dictionary-indexed, coordinates are quantized and delta/zig-zag varint encoded, and the payload is zlib-compressed with a fixed level. A pack is rejected if any province is missing, coordinates are invalid, source feature fields are malformed, or the resulting pack cannot fit the configured budget. Low-priority features may be omitted only when the report records the omission; the builder never invents coverage.

The pack budget is 3,670,016 bytes (3.5 MiB), matching the M02 candidate limit. The 4.75 MiB tiles partition and 320 KiB free-space gate remain firmware/installer concerns for M2/M4. No SD card, app slot, OTA slot, or user partition is touched by this tool.

## Provenance and licensed extraction

For a real build, download the Netherlands PBF from the Geofabrik page or use an eligible PDOK bulk dataset. Keep a sidecar provenance record containing source URL, license, source/data date, retrieval date, exact input SHA256, profile SHA256, builder commit, and output SHA256. Obtain redistribution/attribution approval for each source; keep ODbL attribution and credits in the offline UI. CI intentionally runs only a tiny synthetic fixture. The full licensed-data build is a separately gated workflow job and is never downloaded on ordinary commits.

The map is not activated in the product: a native spatial index, cooperative renderer integration, real geographic coverage and signed installation remain M2/M3 gates. It is not a production map, coverage guarantee, GPS database, or Google-equivalent map.


## Native reader scope

`NanoMapPack` validates the bounded container, metadata, CRC32, name lookups and streaming geometry records. LVGL integration and device scheduling remain later M2/M3 gates.

## Streaming geometry checkpoint

The geometry branch uses MIT-licensed miniz 3.0.2 tinfl pinned to
`293d4db1b7d0ffee9756d035b9ac6f7431ef8492`. The cursor retains a fixed
32 KiB dictionary plus decompressor state; it never retains the entire decoded
body. A query retains one record of at most 4096 quantized points and emits
clipped two-point segments (one-point place features remain points). Name IDs
are one-based with zero meaning unnamed. Coordinates use fixed 100000 units
per degree. Producer inputs with longer features must be split upstream.

The caller must keep the original pack buffer alive and immutable after open.
Container CRC is accidental-corruption detection, not asset authentication.
Queries validate zlib Adler checksum, exact record count and trailing bytes;
return false means any delivered candidate segments must be discarded. Stopping
the callback suppresses further delivery while validation continues. Maximum
decoded work is 4 MiB per query. This synchronous host/embedded module still
needs cooperative scheduling, real map coverage, signed-asset verification,
full firmware integration and hardware/RF concurrency tests. The standalone
ESP32-S3 compile harness is not a complete T-Deck firmware build or HIL test.

## Read-only install manifest map artifact

The verifier `v30/installer/verify_install_manifest.py` accepts a development
map image with these fields. All sizes are bytes.

| Field | Required value |
| --- | --- |
| artifact `kind` | `"map"` |
| artifact `offset` | `8192000` (`0x7D0000`) |
| artifact `size` | `4980736` (`0x4C0000`, 4.75 MiB) |
| artifact `pack_size` | Exact unpadded `build_nl.py` output length, at most `3670016` |
| artifact `padding_byte` | Integer `255` (`0xFF`) |
| artifact `sha256` | SHA256 of the entire padded image |
| manifest `pack_metadata_size` | Exact little-endian metadata length from pack bytes 6..9, `1..65536` |

Construct the image as the unmodified logical pack followed by `0xFF` bytes
until its length is exactly `0x4C0000`. The pack begins with `MMN1`,
little-endian uint16 format version `1`, little-endian uint32 metadata
length, UTF-8 JSON metadata, compressed payload, and a little-endian CRC32
of all preceding logical pack bytes. The metadata's integer `version`
must equal the header version. Padding is outside the logical CRC32.

The verifier checks declared sizes against the file and header, the logical
CRC32, and every padding byte. It does not write flash or authorize installation.
Production manifests remain rejected; CRC32 and hashes are not publisher
signatures. SPIFFS, NVS and other non-allowlisted partition roles remain rejected.
