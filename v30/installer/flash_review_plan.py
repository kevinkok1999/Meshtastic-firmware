#!/usr/bin/env python3
"""Emit a signed-package flash-range review, never a hardware write command."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from verify_install_manifest import (
    BOARD, CHIP, FLASH_SIZE, ManifestError, _int, validate_manifest,
)
from verify_signed_manifest import read_authenticated_manifest


SECTOR = 0x1000
# Half-open, independently frozen target ranges. App1 is deliberately excluded.
WINDOWS = {
    'bootloader': (0, 0x8000), 'partition': (0x8000, 0x9000),
    'ota_data': (0xe000, 0x10000), 'app0': (0x10000, 0x3f0000),
    'map': (0x7d0000, 0xc90000),
}
PRESERVED = (
    ('nvs', 0x9000, 0xe000), ('app1', 0x3f0000, 0x7d0000),
    ('spiffs', 0xc90000, 0xff0000), ('coredump', 0xff0000, 0x1000000),
)


def make_review(manifest_path: Path, signature: Path, public_key: Path,
                pin: str, artifact_root: Path | None = None) -> dict:
    manifest = read_authenticated_manifest(manifest_path, signature, public_key, pin)
    root = artifact_root if artifact_root is not None else manifest_path.parent
    validate_manifest(manifest, root)
    if manifest.get('install_mode') != 'full-v30':
        raise ManifestError('flash-range review requires full-v30')
    segments = []
    for item in manifest['artifacts']:
        kind = item['kind']
        if kind not in WINDOWS:
            raise ManifestError(f'{kind} is not authorized for this review')
        start, stop = WINDOWS[kind]
        offset = _int(item['offset'], kind + '.offset')
        size = _int(item['size'], kind + '.size')
        erase_end = ((offset + size + SECTOR - 1) // SECTOR) * SECTOR
        if offset != start or offset % SECTOR or erase_end > stop:
            raise ManifestError(f'{kind} sector extent escapes its reviewed window')
        if any(offset < end and erase_end > begin for _, begin, end in PRESERVED):
            raise ManifestError(f'{kind} overlaps preserved flash')
        segments.append({'kind': kind, 'offset': offset, 'size': size,
                         'erase_start': offset, 'erase_end': erase_end,
                         'path': item.get('path', item.get('file')),
                         'sha256': item['sha256'].lower()})
    if {p['kind'] for p in segments} != set(WINDOWS):
        raise ManifestError('full-v30 review roles do not match expected windows')
    return {'schema': 1, 'board': BOARD, 'chip': CHIP, 'flash_size_bytes': FLASH_SIZE,
            'publisher_key_sha256': pin, 'flash_authorized': False, 'erase_all': False,
            'development_only': True, 'requires_device_layout_readback': True,
            'segments': sorted(segments, key=lambda p:p['offset']),
            'preserved_ranges': [{'name': name, 'start': start, 'end': end}
                                 for name, start, end in PRESERVED]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ('manifest', 'signature', 'public_key'):
        parser.add_argument(field, type=Path)
    parser.add_argument('--public-key-sha256', required=True)
    parser.add_argument('--artifact-root', type=Path)
    args = parser.parse_args()
    try:
        review = make_review(args.manifest, args.signature, args.public_key,
                             args.public_key_sha256, args.artifact_root)
        print(json.dumps(review, sort_keys=True, indent=2))
    except (OSError, ValueError, ManifestError) as exc:
        print('INVALID: ' + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
