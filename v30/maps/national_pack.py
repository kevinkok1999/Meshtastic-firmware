#!/usr/bin/env python3
"""Offline, bounded national candidate generation. Never downloads or publishes."""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import struct
import zlib

from build_nl import PROVINCES, REQUIRED_KINDS, build_pack
from ingest_osm import HIGHWAY, _inside

MAX_POINTS = 512
CELL = 0.025
MAX_CELLS = 100_000
MAX_SELECTED_POINTS = 2_000_000
MAP_SIZE = 0x4C0000
EXTENT = (3.0, 50.5, 7.5, 54.0)
ATTRIBUTION = "© OpenStreetMap contributors (ODbL 1.0); Kadaster/PDOK (CC BY 4.0)"


def verify_sha256(path, expected):
    if len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected):
        raise ValueError("expected SHA256 must be 64 lowercase hex digits")
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    actual = digest.hexdigest()
    if actual != expected:
        raise ValueError("source SHA256 mismatch")
    return actual


def _name(text):
    text = " ".join(str(text or "").split())
    text = "".join(ch for ch in text if ord(ch) >= 0x20)
    # UTF-8 boundary-safe; never leave an invalid character sequence.
    return text.encode("utf-8")[:256].decode("utf-8", errors="ignore")


class ProvinceLocator:
    """WGS84 polygon inventory, exact membership with optional prepared GEOS."""

    def __init__(self, source, *, require_fast=False):
        if source.get("type") != "FeatureCollection":
            raise ValueError("expected province GeoJSON FeatureCollection")
        self.polygons = {}
        self.bounds = {}
        self.fast = None
        try:
            from shapely.geometry import Point, shape
            from shapely.prepared import prep
        except ImportError:
            if require_fast:
                raise ValueError("national run requires shapely") from None
        else:
            self.fast = {}
            self.point = Point
        for feature in source["features"]:
            props = feature["properties"]
            province = props.get("naam", props.get("province", props.get("name")))
            if province == "Fryslân":
                province = "Friesland"  # existing MMN1 province dictionary
            if province not in PROVINCES or province in self.polygons:
                raise ValueError(f"unknown or duplicate province: {province}")
            geometry = feature["geometry"]
            if geometry["type"] == "Polygon":
                polygons = [geometry["coordinates"]]
            elif geometry["type"] == "MultiPolygon":
                polygons = geometry["coordinates"]
            else:
                raise ValueError("province boundary must be polygonal")
            points = [point for polygon in polygons for ring in polygon for point in ring]
            if not points or any(len(p) != 2 or not -180 <= p[0] <= 180 or not -90 <= p[1] <= 90
                                 for p in points):
                raise ValueError("province boundary contains invalid WGS84 point")
            if any(not poly or any(len(ring) < 4 or ring[0] != ring[-1] for ring in poly)
                   for poly in polygons):
                raise ValueError("province polygon rings must be closed")
            self.polygons[province] = polygons
            self.bounds[province] = (min(p[0] for p in points), min(p[1] for p in points),
                                     max(p[0] for p in points), max(p[1] for p in points))
            if self.fast is not None:
                geom = shape(geometry)
                if not geom.is_valid:
                    raise ValueError(f"invalid province polygon: {province}")
                self.fast[province] = prep(geom)
        if set(self.polygons) != set(PROVINCES):
            raise ValueError("province input must contain exactly all 12 provinces")

    def locate(self, point):
        lon, lat = point
        hits = []
        for province in PROVINCES:
            lo, la, hi, ha = self.bounds[province]
            if not (lo <= lon <= hi and la <= lat <= ha):
                continue
            if self.fast is not None:
                inside = self.fast[province].covers(self.point(lon, lat))
            else:
                inside = any(_inside(point, poly[0]) and not any(
                    _inside(point, hole) for hole in poly[1:]) for poly in self.polygons[province])
            if inside:
                hits.append(province)
        return hits

    def assign(self, points):
        # Use bounded, evenly spaced source vertices; never use a centroid
        # potentially outside a concave or cross-border geometry.
        indices = sorted({i * (len(points) - 1) // 8 for i in range(9)}) if points else []
        counts = Counter(p for i in indices for p in self.locate(points[i]))
        if not counts:
            return None, counts
        return min(counts, key=lambda p: (-counts[p], p)), counts

    def boundary_rows(self, data_date):
        for province in PROVINCES:
            for i, polygon in enumerate(self.polygons[province]):
                # Keep outer rings (including islands), with endpoints/closure.
                yield {"id": f"pdok/{province}/{i}", "kind": "boundary",
                       "province": province, "name": province, "zoom_min": 3,
                       "geometry": simplify(polygon[0], 0.00015),
                       "data_date": data_date}


def simplify(points, threshold=0.000075):
    if not points:
        return []
    result = [list(points[0])]
    # Deterministic radial simplification; retained geometry is approximate.
    for point in points[1:-1]:
        if math.dist(result[-1], point) >= threshold:
            result.append(list(point))
    if len(points) > 1:
        result.append(list(points[-1]))
    if len(points) >= 4 and points[0] == points[-1] and len({tuple(p) for p in result}) < 3:
        # Small island polygons cannot disappear merely because their
        # perimeter is shorter than the simplification threshold.
        result = [list(p) for p in points]
    if len(result) > MAX_POINTS:
        result = [result[i * (len(result) - 1) // (MAX_POINTS - 1)]
                  for i in range(MAX_POINTS)]
    return result


def _inventory(rows):
    result = {p: {} for p in PROVINCES}
    for row in rows:
        province, kind = row["province"], row["kind"]
        result[province][kind] = result[province].get(kind, 0) + 1
    return result


def select_candidates(candidates, locator, *, per_cell=2, data_date="", max_points=MAX_SELECTED_POINTS):
    if not 1 <= per_cell <= 4:
        raise ValueError("per-cell must be 1..4")
    buckets = {}
    eligible = {p: Counter() for p in PROVINCES}
    outside = 0
    cross_border = 0
    stored_points = 0
    for item in candidates:
        kind = item["kind"]
        if kind not in REQUIRED_KINDS or kind == "boundary":
            continue
        points = item["geometry"]
        province, hits = locator.assign(points)
        if province is None:
            outside += 1
            continue
        if len(hits) > 1:
            cross_border += 1
        eligible[province][kind] += 1
        anchor = next((p for p in points if EXTENT[0] <= p[0] <= EXTENT[2] and
                       EXTENT[1] <= p[1] <= EXTENT[3]), None)
        if anchor is None:
            outside += 1
            continue
        # Bound sample count by national grid size and layer. Place nodes
        # keep names; local roads omit high-cardinality names to save metadata.
        cell = (province, kind, int((anchor[0] - EXTENT[0]) / CELL),
                int((anchor[1] - EXTENT[1]) / CELL))
        if cell not in buckets:
            if len(buckets) >= MAX_CELLS:
                raise ValueError("national candidate cell safety limit exceeded")
            buckets[cell] = []
        bucket = buckets[cell]
        # Rank city/town/village ahead of hamlet/suburb where available,
        # then stable hash. No preference dependent on input ordering.
        rank = (int(item.get("rank", 0)),
                hashlib.sha256(str(item["id"]).encode()).hexdigest(), str(item["id"]))
        if len(bucket) >= per_cell and rank >= bucket[-1][0]:
            continue
        row = {"id": str(item["id"]), "kind": kind, "province": province,
               "name": _name(item.get("name")) if kind != "local" else "",
               "zoom_min": max(3, min(19, int(item["zoom_min"]))),
               "geometry": simplify(points), "data_date": data_date}
        bucket.append((rank, row))
        stored_points += len(row["geometry"])
        bucket.sort(key=lambda x: x[0])
        if len(bucket) > per_cell:
            stored_points -= len(bucket[-1][1]["geometry"])
        del bucket[per_cell:]
        if stored_points > max_points:
            raise ValueError("national candidate geometry budget exceeded")
    rows = sorted((row for bucket in buckets.values() for _, row in bucket), key=lambda r: r["id"])
    return rows, {"outside_or_unassigned": outside, "cross_border_candidates": cross_border,
                  "eligible_by_province_kind": {p: dict(eligible[p]) for p in PROVINCES},
                  "selected_by_province_kind": _inventory(rows)}


def _packed_inventory(pack):
    """Measure records from the finished binary, not the selected input."""
    if pack[:4] != b"MMN1" or zlib.crc32(pack[:-4]) & 0xFFFFFFFF != struct.unpack("<I", pack[-4:])[0]:
        raise ValueError("pack format or CRC invalid")
    version, meta_size = struct.unpack("<HI", pack[4:10])
    if version != 1:
        raise ValueError("unsupported map format")
    metadata = json.loads(pack[10:10 + meta_size])
    decoder = zlib.decompressobj()
    body = decoder.decompress(pack[10 + meta_size:-4], 4 * 1024 * 1024 + 1)
    if len(body) > 4 * 1024 * 1024 or not decoder.eof or decoder.unused_data:
        raise ValueError("invalid compressed body")
    pos = 0

    def varint():
        nonlocal pos
        val = 0
        for shift in range(0, 35, 7):
            if pos >= len(body):
                raise ValueError("truncated record")
            byte = body[pos]
            pos += 1
            val |= (byte & 0x7f) << shift
            if not byte & 0x80:
                return val
        raise ValueError("invalid record varint")

    counts = {p: Counter() for p in PROVINCES}
    for _ in range(metadata["features"]):
        name_id, province_index, zoom, geometry_len = (varint() for _ in range(4))
        if name_id > len(metadata["names"]) or province_index >= len(PROVINCES) or not 3 <= zoom <= 19:
            raise ValueError("invalid packed record")
        pos += geometry_len
        kind_size = varint()
        kind = body[pos:pos + kind_size].decode("ascii")
        pos += kind_size
        if kind not in REQUIRED_KINDS:
            raise ValueError("invalid packed kind")
        counts[PROVINCES[province_index]][kind] += 1
    if pos != len(body) or any(not v for v in counts.values()):
        raise ValueError("packed record inventory incomplete")
    return {p: dict(counts[p]) for p in PROVINCES}


def build_candidate(candidates, locator, out, data_date, profile):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    rows, coverage = select_candidates(candidates, locator, data_date=data_date)
    rows += list(locator.boundary_rows(data_date))
    # Reserve named settlements ahead of roads for a useful offline search.
    profile = json.loads(Path(profile).read_text())
    profile["feature_priorities"]["place"] = 95
    normalized = out / "candidate.normalized.jsonl"
    with normalized.open("w", encoding="utf-8") as handle:
        for row in sorted(rows, key=lambda r: r["id"]):
            handle.write(json.dumps(row, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n")
    pack_path = out / "candidate.meshmap"
    report = build_pack(normalized, profile, pack_path)
    pack = pack_path.read_bytes()
    packed = _packed_inventory(pack)
    if any(packed[p].get("boundary", 0) == 0 for p in PROVINCES):
        raise ValueError("pack omitted all boundary geometry for a province")
    image = pack + b"\xff" * (MAP_SIZE - len(pack))
    (out / "candidate.tiles.bin").write_bytes(image)
    result = {**coverage, "selected_by_province_kind": _inventory(rows),
              "packed_by_province_kind": packed, "pack_sha256": report.pack_sha256,
              "pack_size": len(pack), "metadata_size": struct.unpack("<I", pack[6:10])[0],
              "image_sha256": hashlib.sha256(image).hexdigest(), "image_size": len(image),
              "data_date": data_date, "attribution": ATTRIBUTION,
              "coverage_class": "measured-feature-inventory-not-completeness", "production": False}
    (out / "coverage.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return result


def pbf_candidates(source):
    import osmium
    place_ranks = {"city": 0, "town": 1, "village": 2, "hamlet": 3, "suburb": 4}
    major = {**HIGHWAY, **{k + "_link": v for k, v in HIGHWAY.items()}}
    local = {"residential", "unclassified", "living_street"}
    # FileProcessor visits one object at a time and retains only its node index.
    # The national CI first filters the PBF while preserving way references.
    for obj in osmium.FileProcessor(str(source)).with_locations():
        tags = obj.tags
        if obj.is_node():
            place = tags.get("place")
            if place in place_ranks and obj.location.valid() and tags.get("name"):
                yield {"id": f"node/{obj.id}", "kind": "place",
                       "geometry": [[obj.location.lon, obj.location.lat]],
                       "name": tags["name"], "zoom_min": 7 + place_ranks[place],
                       "rank": place_ranks[place]}
        elif obj.is_way():
            road = tags.get("highway")
            if road in major:
                kind, zoom = major[road], 6
            elif road in local:
                kind, zoom = "local", 13
            elif tags.get("waterway") in {"river", "canal", "stream"} or tags.get("natural") == "water":
                kind, zoom = "water", 9
            else:
                continue
            if not obj.nodes or any(not n.location.valid() for n in obj.nodes):
                raise ValueError(f"missing node location: way/{obj.id}")
            yield {"id": f"way/{obj.id}", "kind": kind, "zoom_min": zoom,
                   "name": tags.get("ref", tags.get("name", "")),
                   "geometry": [[n.lon, n.lat] for n in obj.nodes]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pbf")
    parser.add_argument("boundaries")
    parser.add_argument("out")
    parser.add_argument("--data-date", required=True)
    parser.add_argument("--profile", default=Path(__file__).with_name("nl_profile.json"))
    parser.add_argument("--pbf-sha256", required=True)
    parser.add_argument("--boundaries-sha256", required=True)
    args = parser.parse_args()
    pbf_hash = verify_sha256(args.pbf, args.pbf_sha256)
    boundaries_hash = verify_sha256(args.boundaries, args.boundaries_sha256)
    locator = ProvinceLocator(json.loads(Path(args.boundaries).read_text()), require_fast=True)
    result = build_candidate(pbf_candidates(args.pbf), locator, args.out, args.data_date, args.profile)
    result.update({"filtered_input_sha256": pbf_hash, "boundaries_input_sha256": boundaries_hash})
    Path(args.out, "coverage.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
