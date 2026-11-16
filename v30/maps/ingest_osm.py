#!/usr/bin/env python3
"""Offline OSM XML/PBF -> MeshMap Nano normalized JSONL adapter.

No network access is performed. PBF input requires the optional pyosmium package.
The emitted contract is consumed by build_nl.py.
"""
from __future__ import annotations
import argparse, hashlib, json, sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
import xml.etree.ElementTree as ET

PROVINCES = ("Drenthe", "Flevoland", "Friesland", "Gelderland", "Groningen",
             "Limburg", "Noord-Brabant", "Noord-Holland", "Overijssel",
             "Utrecht", "Zeeland", "Zuid-Holland")
ALIASES = {p.lower(): p for p in PROVINCES}
ALIASES.update({"brabant": "Noord-Brabant", "noord brabant": "Noord-Brabant",
                "noord holland": "Noord-Holland", "zuid holland": "Zuid-Holland"})
HIGHWAY = {"motorway": "motorway", "trunk": "trunk", "primary": "primary",
           "secondary": "secondary", "tertiary": "tertiary"}
WATER = {"waterway", "natural_water", "water", "reservoir", "riverbank"}

class IngestError(ValueError):
    pass

def _province(tags: dict[str, str]) -> str | None:
    for key in ("province", "addr:province", "is_in:state", "is_in:province"):
        value = " ".join(tags.get(key, "").replace("-", " ").split()).lower()
        if value in ALIASES:
            return ALIASES[value]
    return None

def _kind(tags: dict[str, str]) -> str | None:
    if tags.get("boundary") in ("administrative", "political"):
        return "boundary"
    if tags.get("highway") in HIGHWAY:
        return HIGHWAY[tags["highway"]]
    if tags.get("waterway") or tags.get("natural") in WATER:
        return "water"
    if tags.get("place") in ("city", "town", "village", "hamlet", "suburb"):
        return "place"
    return "local" if tags.get("highway") else None

def _tags(element: ET.Element) -> dict[str, str]:
    return {t.attrib.get("k", ""): t.attrib.get("v", "") for t in element.findall("tag")}

def parse_osm_xml(source: Path) -> list[dict[str, Any]]:
    nodes: dict[str, tuple[float, float, dict[str, str]]] = {}
    ways: list[ET.Element] = []
    for event, element in ET.iterparse(source, events=("end",)):
        if element.tag == "node":
            try:
                lon, lat = float(element.attrib["lon"]), float(element.attrib["lat"])
            except (KeyError, ValueError):
                raise IngestError("node has invalid coordinates") from None
            nodes[element.attrib["id"]] = (lon, lat, _tags(element))
        elif element.tag == "way":
            ways.append(element)
        element.clear()
    features: list[dict[str, Any]] = []
    for node_id, (lon, lat, tags) in sorted(nodes.items()):
        kind = _kind(tags)
        province = _province(tags)
        if kind == "place" and province:
            features.append({"id": f"node/{node_id}", "kind": kind, "province": province,
                             "name": " ".join(tags.get("name", "").split()), "zoom_min": 8,
                             "geometry": [[lon, lat]]})
    for way in sorted(ways, key=lambda x: x.attrib.get("id", "")):
        tags = _tags(way)
        kind, province = _kind(tags), _province(tags)
        refs = [n.attrib.get("ref") for n in way.findall("nd")]
        if not kind or not province or not refs or any(ref not in nodes for ref in refs):
            continue
        geometry = [[nodes[ref][0], nodes[ref][1]] for ref in refs]
        features.append({"id": f"way/{way.attrib['id']}", "kind": kind, "province": province,
                         "name": " ".join(tags.get("name", "").split()),
                         "zoom_min": 8 if kind in ("local", "place") else 5,
                         "geometry": geometry})
    if not features:
        raise IngestError("source produced no supported, province-attributed features")
    return features

def parse_pbf(source: Path) -> list[dict[str, Any]]:
    try:
        import osmium  # type: ignore
    except ImportError as exc:
        raise IngestError("PBF input requires optional pyosmium; use .osm XML with stdlib parser in CI") from exc
    features: list[dict[str, Any]] = []
    class Handler(osmium.SimpleHandler):
        def way(self, way):
            tags = dict(way.tags)
            province, kind = _province(tags), _kind(tags)
            if not province or not kind:
                return
            points = [[n.lon, n.lat] for n in way.nodes if n.location.valid()]
            if points:
                features.append({"id": f"way/{way.id}", "kind": kind, "province": province,
                                 "name": " ".join(tags.get("name", "").split()),
                                 "zoom_min": 8 if kind == "local" else 5, "geometry": points})
    Handler().apply_file(str(source), locations=True)
    if not features:
        raise IngestError("PBF produced no supported, province-attributed features")
    return sorted(features, key=lambda item: item["id"])

def ingest(source: str | Path, output: str | Path, source_url: str, license_name: str,
           data_date: str, retrieved_at: str | None = None) -> dict[str, Any]:
    source_path, output_path = Path(source), Path(output)
    raw = source_path.read_bytes()
    source_sha = hashlib.sha256(raw).hexdigest()
    features = parse_pbf(source_path) if source_path.suffix.lower() == ".pbf" else parse_osm_xml(source_path)
    lines = b"".join(json.dumps(feature, sort_keys=True, separators=(",", ":")).encode() + b"\n"
                    for feature in sorted(features, key=lambda item: item["id"]))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(lines)
    retrieved_at = retrieved_at or datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    provenance = {"contract": "meshtastic-v30-normalized-jsonl/v1", "source_url": source_url,
                  "license": license_name, "data_date": data_date, "retrieved_at": retrieved_at,
                  "source_sha256": source_sha, "normalized_sha256": hashlib.sha256(lines).hexdigest(),
                  "feature_count": len(features), "parser": "ingest_osm.py",
                  "network_access": False, "provenance_complete": True}
    output_path.with_suffix(output_path.suffix + ".provenance.json").write_text(
        json.dumps(provenance, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return provenance

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source"); parser.add_argument("output")
    parser.add_argument("--source-url", required=True); parser.add_argument("--license", required=True)
    parser.add_argument("--data-date", required=True); parser.add_argument("--retrieved-at")
    args = parser.parse_args()
    print(json.dumps(ingest(args.source, args.output, args.source_url, args.license,
                             args.data_date, args.retrieved_at), indent=2, sort_keys=True))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
