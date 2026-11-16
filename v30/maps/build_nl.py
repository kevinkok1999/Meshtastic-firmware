#!/usr/bin/env python3
"""Deterministic MeshMap Nano normalized-JSONL pack builder.

The builder intentionally performs no network access. Licensed extraction is an
external, auditable step; this module only validates and packs normalized data.
"""
from __future__ import annotations
import argparse, hashlib, json, math, struct, zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

MAGIC = b"MMN1"
FORMAT_VERSION = 1
PROVINCES = (
    "Drenthe", "Flevoland", "Friesland", "Gelderland", "Groningen",
    "Limburg", "Noord-Brabant", "Noord-Holland", "Overijssel",
    "Utrecht", "Zeeland", "Zuid-Holland",
)
DEFAULT_BUDGET = 3_670_016
REQUIRED_KINDS = {"boundary", "motorway", "trunk", "primary", "secondary",
                  "tertiary", "local", "water", "place"}

class BuildError(ValueError):
    pass

@dataclass(frozen=True)
class BuildReport:
    bytes_written: int
    budget_bytes: int
    source_features: int
    packed_features: int
    omitted_features: int
    omitted_by_kind: dict[str, int]
    names: int
    provinces: tuple[str, ...]
    roads_by_class: dict[str, int]
    extent: tuple[float, float, float, float]
    zoom_inventory: dict[str, int]
    data_date: str | None
    source_sha256: str
    profile_sha256: str
    pack_sha256: str

    def as_dict(self) -> dict[str, Any]:
        return {
            "bytes_written": self.bytes_written,
            "budget_bytes": self.budget_bytes,
            "source_features": self.source_features,
            "packed_features": self.packed_features,
            "omitted_features": self.omitted_features,
            "omitted_by_kind": dict(sorted(self.omitted_by_kind.items())),
            "names": self.names,
            "provinces": list(self.provinces),
            "roads_by_class": dict(sorted(self.roads_by_class.items())),
            "extent": list(self.extent),
            "zoom_inventory": dict(sorted(self.zoom_inventory.items())),
            "data_date": self.data_date,
            "source_sha256": self.source_sha256,
            "profile_sha256": self.profile_sha256,
            "pack_sha256": self.pack_sha256,
        }

def _load_profile(profile: str | Path | dict[str, Any]) -> tuple[dict[str, Any], bytes]:
    if isinstance(profile, (str, Path)):
        raw = Path(profile).read_bytes()
        obj = json.loads(raw)
    else:
        obj = profile
        raw = json.dumps(obj, sort_keys=True, separators=(",", ":")).encode()
    if tuple(obj.get("provinces", ())) != PROVINCES:
        raise BuildError("profile must define exactly the 12 European Netherlands provinces")
    budget = int(obj.get("pack_budget_bytes", DEFAULT_BUDGET))
    if budget <= 0:
        raise BuildError("pack_budget_bytes must be positive")
    return obj, raw

def _varuint(value: int) -> bytes:
    if value < 0:
        raise BuildError("unsigned varint received negative value")
    out = bytearray()
    while value >= 0x80:
        out.append((value & 0x7f) | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)

def _zigzag(value: int) -> int:
    return (value << 1) ^ (value >> 63)

def _geometry(points: Iterable[Iterable[Any]], scale: int) -> tuple[bytes, tuple[float, float, float, float]]:
    previous = (0, 0)
    result = bytearray()
    extent = [float("inf"), float("inf"), float("-inf"), float("-inf")]
    count = 0
    for pair in points:
        if not isinstance(pair, (list, tuple)) or len(pair) != 2:
            raise BuildError("geometry point must be [longitude, latitude]")
        lon, lat = float(pair[0]), float(pair[1])
        if not (math.isfinite(lon) and math.isfinite(lat) and -180 <= lon <= 180 and -90 <= lat <= 90):
            raise BuildError("invalid coordinate")
        current = (round(lon * scale), round(lat * scale))
        result.extend(_varuint(_zigzag(current[0] - previous[0])))
        result.extend(_varuint(_zigzag(current[1] - previous[1])))
        previous = current
        extent[0], extent[1] = min(extent[0], lon), min(extent[1], lat)
        extent[2], extent[3] = max(extent[2], lon), max(extent[3], lat)
        count += 1
    if count == 0:
        raise BuildError("geometry cannot be empty")
    return _varuint(count) + bytes(result), tuple(extent)

def _read_features(source: Path, profile: dict[str, Any]) -> tuple[list[dict[str, Any]], str, str | None]:
    raw = source.read_bytes()
    features: list[dict[str, Any]] = []
    seen_ids: set[str] = set()
    data_dates: set[str] = set()
    for line_no, line in enumerate(raw.splitlines(), 1):
        if not line.strip():
            continue
        try:
            item = json.loads(line)
        except json.JSONDecodeError as exc:
            raise BuildError(f"line {line_no}: invalid JSON: {exc.msg}") from exc
        if not isinstance(item, dict):
            raise BuildError(f"line {line_no}: feature must be an object")
        for field in ("id", "kind", "province", "geometry"):
            if field not in item:
                raise BuildError(f"line {line_no}: missing {field}")
        if not isinstance(item["id"], str) or not item["id"]:
            raise BuildError(f"line {line_no}: id must be non-empty string")
        if item["id"] in seen_ids:
            raise BuildError("line %d: duplicate feature id %r" % (line_no, item["id"]))
        seen_ids.add(item["id"])
        if item["kind"] not in REQUIRED_KINDS:
            raise BuildError(f"line {line_no}: unsupported kind {item['kind']!r}")
        if item["province"] not in PROVINCES:
            raise BuildError(f"line {line_no}: unknown province {item['province']!r}")
        if "data_date" in item:
            data_dates.add(str(item["data_date"]))
        geometry, bbox = _geometry(item["geometry"], int(profile.get("quantization", 100000)))
        features.append({
            "id": item["id"], "kind": item["kind"], "province": item["province"],
            "name": str(item.get("name", "")), "zoom_min": int(item.get("zoom_min", 3)),
            "geometry": geometry, "bbox": bbox,
        })
    if not features:
        raise BuildError("source contains no features")
    missing = sorted(set(PROVINCES) - {f["province"] for f in features})
    if missing:
        raise BuildError("missing province coverage: " + ", ".join(missing))
    if len(data_dates) > 1:
        raise BuildError("source contains conflicting data_date values")
    return features, hashlib.sha256(raw).hexdigest(), next(iter(data_dates), None)

def _encode(features: list[dict[str, Any]], profile: dict[str, Any]) -> tuple[bytes, int, dict[str, int], int, dict[str, int]]:
    names = sorted({f["name"] for f in features if f["name"]})
    name_index = {name: i for i, name in enumerate(names)}
    priorities = profile.get("feature_priorities", {})
    ordered = sorted(features, key=lambda f: (-int(priorities.get(f["kind"], 0)), f["id"]))
    body = bytearray()
    packed = 0
    for f in ordered:
        name_id = name_index.get(f["name"], -1) + 1
        body.extend(_varuint(name_id))
        body.extend(_varuint(PROVINCES.index(f["province"])))
        body.extend(_varuint(int(f["zoom_min"])))
        body.extend(_varuint(len(f["geometry"])))
        body.extend(f["geometry"])
        kind = f["kind"].encode()
        body.extend(_varuint(len(kind)))
        body.extend(kind)
        packed += 1
    compressed = zlib.compress(bytes(body), level=9)
    omitted_by_kind: dict[str, int] = {}
    return compressed, packed, omitted_by_kind, len(names), {k: sum(f["kind"] == k for f in features) for k in REQUIRED_KINDS}

def _pack_bytes(features: list[dict[str, Any]], profile: dict[str, Any], source_sha: str, data_date: str | None) -> tuple[bytes, int, dict[str, int], int, dict[str, int], list[dict[str, Any]]]:
    priorities = profile.get("feature_priorities", {})
    working = list(features)
    omitted: dict[str, int] = {}
    budget = int(profile.get("pack_budget_bytes", DEFAULT_BUDGET))
    while True:
        encoded, packed, _ignored, names_count, roads = _encode(working, profile)
        names = sorted({f["name"] for f in working if f["name"]})
        extent = [min(f["bbox"][0] for f in working), min(f["bbox"][1] for f in working), max(f["bbox"][2] for f in working), max(f["bbox"][3] for f in working)]
        meta = {"version": FORMAT_VERSION, "profile_version": profile.get("profile_version", 1), "source_sha256": source_sha, "data_date": data_date, "provinces": list(PROVINCES), "names": names, "features": packed, "extent": extent, "zoom_inventory": {str(z): sum(1 for f in working if f["zoom_min"] <= z) for z in range(3, 20)}}
        meta_bytes = json.dumps(meta, sort_keys=True, separators=(",", ":")).encode()
        candidate = MAGIC + struct.pack("<HI", FORMAT_VERSION, len(meta_bytes)) + meta_bytes + encoded
        if len(candidate) <= budget:
            return candidate, packed, omitted, names_count, roads, working
        if len(working) <= 1:
            raise BuildError(f"pack exceeds budget ({len(candidate)} > {budget})")
        lowest = min(working, key=lambda f: (int(priorities.get(f["kind"], 0)), f["id"]))
        working.remove(lowest)
        omitted[lowest["kind"]] = omitted.get(lowest["kind"], 0) + 1
        if not any(f["province"] == lowest["province"] for f in working):
            raise BuildError("budget would remove final feature for province " + lowest["province"])

def build_pack(source_path: str | Path, profile: str | Path | dict[str, Any], out_path: str | Path) -> BuildReport:
    source = Path(source_path)
    profile_obj, profile_raw = _load_profile(profile)
    features, source_sha, data_date = _read_features(source, profile_obj)
    pack, packed, omitted, names, roads, retained = _pack_bytes(features, profile_obj, source_sha, data_date)
    out = Path(out_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(pack)
    extent = (
        min(f["bbox"][0] for f in retained), min(f["bbox"][1] for f in retained),
        max(f["bbox"][2] for f in retained), max(f["bbox"][3] for f in retained),
    )
    zoom_inventory = {str(z): sum(1 for f in retained if f["zoom_min"] <= z) for z in range(3, 20)}
    report = BuildReport(
        bytes_written=len(pack), budget_bytes=int(profile_obj.get("pack_budget_bytes", DEFAULT_BUDGET)),
        source_features=len(features), packed_features=packed, omitted_features=sum(omitted.values()),
        omitted_by_kind=omitted, names=names, provinces=PROVINCES, roads_by_class=roads,
        extent=extent, zoom_inventory=zoom_inventory, data_date=data_date,
        source_sha256=source_sha, profile_sha256=hashlib.sha256(profile_raw).hexdigest(),
        pack_sha256=hashlib.sha256(pack).hexdigest())
    out.with_suffix(out.suffix + ".report.json").write_text(
        json.dumps(report.as_dict(), sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return report

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_jsonl"); parser.add_argument("profile_json"); parser.add_argument("output_pack")
    args = parser.parse_args()
    report = build_pack(args.source_jsonl, args.profile_json, args.output_pack)
    print(json.dumps(report.as_dict(), sort_keys=True, indent=2))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
