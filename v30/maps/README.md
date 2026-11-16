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

This M1 checkpoint is not activated for firmware: it has no native spatial index, bounded streaming decoder, or renderer integration yet; those are M2/M3 gates. It is not a production map, coverage guarantee, GPS database, or Google-equivalent map.
