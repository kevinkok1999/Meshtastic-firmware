# Nationwide candidate build

This is an **unsigned development candidate**, not an end-user map or a
claim of street-level coverage. No SD card is required by the pack format.

## Sources (European Netherlands)

- [Geofabrik Netherlands](https://download.geofabrik.de/europe/netherlands.html):
  dated raw OpenStreetMap extracts under ODbL 1.0. The actual dated file
  `netherlands-261116.osm.pbf` is 1.4 GB, so acquisition is not performed on
  ordinary pushes. Do not use the changing latest alias for a repeat build.
- [PDOK Bestuurlijke Gebieden](https://www.pdok.nl/ogc-apis/-/article/bestuurlijke-gebieden):
  the official province collection's CRS84/GeoJSON polygons. The
  [collection metadata](https://api.pdok.nl/kadaster/brk-bestuurlijke-gebieden/ogc/v1/collections?f=json)
  declares CC BY 4.0. Preserve Kadaster/PDOK credits.
- No commercial tiles, addresses/buildings dataset, Google Maps content,
  tile scraping or data copied from proprietary services.

## Build

`V30 national map candidate` CI runs local synthetic fixtures including a
real binary PBF conversion and reference-preserving filter. The full
national job is manual only. Enter the dated PBF filename date (YYMMDD),
and reviewed SHA256 values of the exact PBF and exact PDOK GeoJSON response.
A changing upstream response will intentionally fail verification.
Archive both inputs independently if reproducibility across days is
required. GitHub workflow_dispatch must be enabled by repository
configuration; when the workflow is not on the default branch, execute
the same documented commands in a controlled development runner.

The job uses osmium tags-filter retaining referenced nodes to avoid
processing every building and node in Python, then streams raw PBF
features through a prepared GEOS polygon lookup and deterministic
geographical grid sampler. Sampling is bounded by grid/layer, per-cell
quota and geometry point caps; road geometry is approximate and local
road labels are omitted to preserve scarce dictionary bytes. Named
settlements receive priority ahead of roads in the effective profile.
Offshore/unassigned and multi-province ways are counted rather than
silently creating province memberships from a centroid. Boundary shapes
come from official polygons, including multipolygon exterior rings,
with deterministic simplification. Relation-based water area geometries
and municipal boundaries are not reconstructed by this candidate.

The builder's own 3.5 MiB compressed, 64 KiB metadata and 4 MiB decoded
limits remain enforced. The final MMN1 body is independently decoded to
measure packed per-province/per-layer feature counts. The build fails if
a province loses its entire boundary layer. Exact mixed layer coverage,
road kilometer coverage, municipalities, geodesic simplification error,
full real-data end-to-end usability, native rendering and signature
checks still require additional gates.

Artifacts:

- `candidate.normalized.jsonl`: derived candidate input records (not
  the full OSM source); keep with the pack to help meet ODbL obligations.
- `candidate.meshmap` and report: bounded logical map pack.
- `candidate.tiles.bin`: same pack with 0xFF padding to 0x4C0000 bytes.
- `coverage.json`: observed sample/packed counts, hashes, lengths,
  attribution and explicit non-production flag.
- `provenance.json`: source URLs, dates, input SHA256, exact commit,
  tool versions, attribution and output SHA256.

No signing, flashing, release publication or website deployment is
performed. The full national run is not yet verified until a successful
manual acquisition and artifact is inspected.
