#!/usr/bin/env python3
"""Repack an immutable, preselected national JSONL; never fetch or publish data."""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

from build_nl import (PROVINCES, REQUIRED_KINDS, MAX_METADATA_BYTES,
                      MAX_DECODED_BODY_BYTES, _load_profile, _read_features,
                      _pack_bytes, _varuint, BuildError)
from national_pack import _packed_inventory, MAP_SIZE, ATTRIBUTION, verify_sha256


REF = re.compile(r'[AN]\s*\d{1,4}(?:[a-zA-Z])?(?:\s*;\s*[AN]\s*\d{1,4})*\Z')
ROADS = {'motorway', 'trunk', 'primary', 'secondary', 'tertiary', 'local'}


def _name_cost(name):
    return len(json.dumps(name, ensure_ascii=False).encode('utf-8')) + 1


def _bounded_names(rows, budget=MAX_METADATA_BYTES - 1024):
    """Keep settlements first, then shared road references, then repeated labels."""
    names = set()
    uses = Counter(row['name'] for row in rows if row['name'])
    remaining = budget

    def add(name):
        nonlocal remaining
        if name and name not in names and _name_cost(name) <= remaining:
            names.add(name)
            remaining -= _name_cost(name)

    for row in rows:
        if row['kind'] == 'boundary':
            add(row['name'])
    places = defaultdict(list)
    for row in rows:
        if row['kind'] == 'place':
            places[row['province']].append(row)
    # At each zoom, round-robin provinces, avoiding a geographical label bias.
    for zoom in range(3, 20):
        queues = [sorted((row['name'], row['id']) for row in places[p]
                         if row['zoom_min'] == zoom) for p in PROVINCES]
        for i in range(max((len(q) for q in queues), default=0)):
            for q in queues:
                if i < len(q):
                    add(q[i][0])
    refs = {row['name'] for row in rows if row['kind'] in ROADS and REF.fullmatch(row['name'])}
    for name in sorted(refs, key=lambda n: (-uses[n], _name_cost(n), n)):
        add(name)
    # Keep recurring navigational labels only after settlements and references.
    for name in sorted((n for n, count in uses.items() if count >= 3),
                       key=lambda n: (-uses[n], _name_cost(n), n)):
        add(name)
    return [{**row, 'name': row['name'] if row['name'] in names else ''} for row in rows]


def _body_cost(row):
    # Conservative: name dictionary index uses at most three varint bytes.
    n = len(row['geometry'])
    k = len(row['kind'])
    return (3 if row['name'] else 1) + 2 + len(_varuint(n)) + n + len(_varuint(k)) + k


def _balanced_geometry(rows, body_limit):
    if sum(map(_body_cost, rows)) <= body_limit:
        return rows
    strata = defaultdict(list)
    for row in rows:
        strata[row['province'], row['kind']].append(row)
    for key, group in strata.items():
        # The input is already spatially stratified; hash sampling avoids ID bias.
        group.sort(key=lambda x: (hashlib.sha256(x['id'].encode()).digest(), x['id']))

    def selection(fraction):
        return [row for key, group in sorted(strata.items())
                for row in group[:len(group) if key[1] in {'boundary', 'place'} else
                                 max(1, len(group) * fraction // 10000)]]

    if sum(map(_body_cost, selection(0))) > body_limit:
        raise BuildError('budget cannot preserve settlements, boundaries and every input stratum')
    lo, hi = 0, 10000
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if sum(map(_body_cost, selection(mid))) <= body_limit:
            lo = mid
        else:
            hi = mid - 1
    return selection(lo)


def _counts(rows):
    counts = {p: Counter() for p in PROVINCES}
    for row in rows:
        counts[row['province']][row['kind']] += 1
    return {p: dict(counts[p]) for p in PROVINCES}


def repack(source, profile_path, output, expected_sha):
    verify_sha256(source, expected_sha)
    profile, profile_bytes = _load_profile(profile_path)
    profile['feature_priorities'] = {**profile['feature_priorities'], 'place': 95}
    rows, source_sha, date = _read_features(Path(source), profile)
    if source_sha != expected_sha:
        raise BuildError('normalized source hash changed during read')
    selected = _bounded_names(rows)
    retained = _balanced_geometry(selected, MAX_DECODED_BODY_BYTES)
    pack, count, omitted, _, _, actual = _pack_bytes(retained, profile, source_sha, date)
    if omitted:
        # Never accept global pruning. Source sizes above the compressed budget
        # get a conservative raw cap instead; this path retains all strata.
        retained = _balanced_geometry(selected, min(MAX_DECODED_BODY_BYTES,
                            int(profile['pack_budget_bytes']) - MAX_METADATA_BYTES - 4096))
        pack, count, omitted, _, _, actual = _pack_bytes(retained, profile, source_sha, date)
    if omitted or len(actual) != len(retained):
        raise BuildError('legacy builder globally pruned balanced selection')
    inventory, input_counts = _packed_inventory(pack), _counts(rows)
    if inventory != _counts(retained) or any(set(inventory[p]) != set(input_counts[p]) for p in PROVINCES):
        raise BuildError('packed inventory lost a represented province/category or disagrees with selection')
    meta_size = struct.unpack('<I', pack[6:10])[0]
    meta = json.loads(pack[10:10+meta_size])
    body = zlib.decompress(pack[10+meta_size:-4])
    image = pack.ljust(MAP_SIZE, b'\xff')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'candidate.meshmap').write_bytes(pack)
    (output / 'candidate.tiles.bin').write_bytes(image)
    result = {'production': False, 'coverage_class': 'measured-feature-inventory-not-completeness',
              'data_date': date, 'attribution': ATTRIBUTION, 'source_sha256': source_sha,
              'profile_sha256': hashlib.sha256(profile_bytes).hexdigest(),
              'source_features': len(rows), 'packed_features': count,
              'omitted_features': len(rows) - count,
              'selected_by_province_kind': input_counts, 'packed_by_province_kind': inventory,
              'source_named_by_province_kind': _counts([r for r in rows if r['name']]),
              'packed_named_by_province_kind': _counts([r for r in retained if r['name']]),
              'pack_sha256': hashlib.sha256(pack).hexdigest(), 'pack_size': len(pack),
              'image_sha256': hashlib.sha256(image).hexdigest(), 'image_size': len(image),
              'metadata_size': meta_size, 'decoded_body_bytes': len(body),
              'names': len(meta['names']), 'zoom_inventory': meta['zoom_inventory'],
              'extent': meta['extent'], 'quota_policy': 'all geometries if reader budget allows; otherwise all boundaries/places plus proportional province-kind quotas'}
    report = {**result, 'bytes_written': len(pack), 'budget_bytes': profile['pack_budget_bytes'],
              'provinces': list(PROVINCES), 'roads_by_class': dict(Counter(r['kind'] for r in retained)),
              'omitted_by_kind': dict(Counter(r['kind'] for r in rows) - Counter(r['kind'] for r in retained))}
    (output / 'coverage.json').write_text(json.dumps(result, sort_keys=True, indent=2)+'\n')
    (output / 'candidate.meshmap.report.json').write_text(json.dumps(report, sort_keys=True, indent=2)+'\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('profile', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--expected-sha256', required=True)
    args = parser.parse_args()
    print(json.dumps(repack(args.source, args.profile, args.output, args.expected_sha256), sort_keys=True, indent=2))


if __name__ == '__main__':
    main()
