#!/usr/bin/env python3
"""Derive a QA-only map pin from an exact reviewed CI artifact.

Caller must authenticate the GitHub run and artifact before supplying files.
The ephemeral signing key exists only within this QA process; it is NOT a
production publisher key and must never be used as a release trust anchor.
"""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from build_dev_package import build
from verify_install_manifest import MAP_SIZE, MAX_PACK_SIZE, ManifestError, validate_manifest
from verify_signed_manifest import bounded_read, strict_object, reject_constant, verified_map_pin

MAP_SOURCE_COMMIT = '97ff3a4d94d2b9b8397c15f4894d980ca8766dcc'
FIRMWARE_SOURCE_COMMIT = 'a673a9ab3c52e93771e45003c617b7f97bb827fb'
PBF_SHA = '2d0528596573987ae547ba4dc16b50c347ac97cfc3fc864d470f184dc8e5c1c7'
BOUNDARY_SHA = '845bbe3287d77ee4bb16b75374cc55a5f6464ab4e95d1a634636de18e2aa9400'
PROVINCES = {'Drenthe','Flevoland','Friesland','Gelderland','Groningen','Limburg',
             'Noord-Brabant','Noord-Holland','Overijssel','Utrecht','Zeeland','Zuid-Holland'}
ROADS = ('motorway','trunk','primary','secondary','tertiary','local')
CATEGORIES = set(ROADS) | {'boundary','place','water'}


def metadata(path):
    return json.loads(bounded_read(path, 256*1024, path.name), object_pairs_hook=strict_object,
                      parse_constant=reject_constant)


def verify_map(root):
    c, p = metadata(root/'coverage.json'), metadata(root/'provenance.json')
    pack = bounded_read(root/'candidate.meshmap', MAX_PACK_SIZE, 'logical map')
    image = bounded_read(root/'candidate.tiles.bin', MAP_SIZE, 'padded map')
    a, b = hashlib.sha256(pack).hexdigest(), hashlib.sha256(image).hexdigest()
    if (c.get('production') is not False or p.get('production') is not False or
        p.get('source_commit') != MAP_SOURCE_COMMIT or p.get('osm_original_sha256') != PBF_SHA or
        p.get('boundary_sha256') != BOUNDARY_SHA or c.get('boundaries_input_sha256') != BOUNDARY_SHA or
        p.get('osm_filtered_sha256') != c.get('filtered_input_sha256') or
        p.get('source_hash_review_mode') != 'reviewed-exact-sha256'):
        raise ManifestError('national candidate provenance mismatch')
    if (not pack.startswith(b'MMN1') or len(image) != MAP_SIZE or image != pack.ljust(MAP_SIZE,b'\xff') or
        c.get('pack_size') != len(pack) or c.get('image_size') != len(image) or
        c.get('pack_sha256') != a or p.get('pack_sha256') != a or
        c.get('image_sha256') != b or p.get('padded_image_sha256') != b):
        raise ManifestError('national map hashes, lengths or padding mismatch')
    inv = c.get('packed_by_province_kind')
    if not isinstance(inv,dict) or set(inv) != PROVINCES:
        raise ManifestError('national map inventory misses provinces')
    for province, kinds in inv.items():
        if (not isinstance(kinds,dict) or set(kinds) != CATEGORIES or
                any(type(v) is not int or v <= 0 for v in kinds.values())):
            raise ManifestError(f'national map inventory inadequate for {province}')
    return a


def prepare_pin(initial, maps):
    expected = verify_map(maps)
    m = metadata(initial/'manifest.json')
    validate_manifest(m, initial)
    if m.get('install_mode') != 'initial' or m.get('source_commit') != FIRMWARE_SOURCE_COMMIT:
        raise ManifestError('initial firmware source mismatch')
    parts = {x['kind']: initial / x['path'] for x in m['artifacts']}
    with tempfile.TemporaryDirectory(prefix='v30-ci-qa-') as folder:
        root = Path(folder)
        manifest = build(root/'candidate',parts['bootloader'],parts['partition'],parts['ota_data'],
                         parts['app0'],maps/'candidate.meshmap')
        key = Ed25519PrivateKey.generate()
        pub = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        (root/'pub').write_bytes(pub)
        (root/'sig').write_bytes(key.sign(manifest.read_bytes()))
        pin = verified_map_pin(manifest,root/'sig',root/'pub',hashlib.sha256(pub).hexdigest())
        if pin != expected:
            raise ManifestError('QA signed logical pin does not equal reviewed artifact pin')
        return pin


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--initial',required=True,type=Path)
    p.add_argument('--map-dir',required=True,type=Path)
    a=p.parse_args()
    print(prepare_pin(a.initial,a.map_dir))
