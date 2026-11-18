#!/usr/bin/env python3
"""Build and validate an unsigned V30 development candidate; never flash a device."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile

from verify_install_manifest import (
    APP_HEADROOM, APP_PARTITION_SIZE, BOARD, CHIP, EXPECTED_OFFSETS,
    FLASH_SIZE, MAP_OFFSET, MAP_SIZE, MAX_PACK_SIZE, ManifestError,
    validate_manifest,
)


def bounded_input(path: Path, limit: int, field: str) -> bytes:
    with path.open('rb') as handle:
        data = handle.read(limit + 1)
    if not data or len(data) > limit:
        raise ManifestError(f'{field} exceeds input budget or is empty')
    return data


def build(output: Path, bootloader: Path, partition: Path, ota_data: Path,
          app: Path, map_pack: Path) -> Path:
    # An already existing path, including a symlink, may never be replaced.
    if output.exists() or output.is_symlink():
        raise ManifestError('output already exists')
    inputs = (
        ('bootloader', bootloader, 0x8000),
        ('partition', partition, 0x1000),
        ('ota_data', ota_data, 0x2000),
        ('app0', app, APP_PARTITION_SIZE - APP_HEADROOM),
    )
    pack = bounded_input(map_pack, MAX_PACK_SIZE, 'map')
    if len(pack) <= 14:
        raise ManifestError('map pack too short')
    metadata_size = struct.unpack_from('<I', pack, 6)[0]
    manifest = {'schema': 1, 'board': BOARD, 'chip': CHIP,
                'flash_size_bytes': FLASH_SIZE, 'production': False,
                'install_mode': 'full-v30', 'pack_metadata_size': metadata_size,
                'artifacts': []}
    with tempfile.TemporaryDirectory(prefix='.v30-stage-', dir=output.parent) as staging:
        stage = Path(staging)

        def add(kind: str, data: bytes, offset: int, extra: dict | None = None) -> None:
            path = kind + '.bin'
            (stage / path).write_bytes(data)
            item = {'kind': kind, 'path': path, 'size': len(data), 'offset': offset,
                    'sha256': hashlib.sha256(data).hexdigest()}
            item.update(extra or {})
            manifest['artifacts'].append(item)

        for kind, path, limit in inputs:
            add(kind, bounded_input(path, limit, kind), EXPECTED_OFFSETS[kind])
        add('map', pack.ljust(MAP_SIZE, b'\xff'), MAP_OFFSET,
            {'pack_size': len(pack), 'padding_byte': 255})
        validate_manifest(manifest, stage)
        manifest_name = 'manifest.json'
        (stage / manifest_name).write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n',
                                         encoding='utf-8')
        # Reserve the target exclusively. Hard-linking the staged bytes avoids
        # clobbering a file even if an external process races us. Publish the
        # manifest last so an interrupted copy cannot be mistaken for a package.
        output.mkdir(exist_ok=False)
        published = []
        try:
            names = [a['path'] for a in manifest['artifacts']] + [manifest_name]
            for name in names:
                dest = output / name
                os.link(stage / name, dest)
                published.append(dest)
        except OSError:
            for dest in reversed(published):
                dest.unlink()
            try:
                output.rmdir()
            except OSError:
                pass  # Never delete any unexpected concurrent content.
            raise
    return output / 'manifest.json'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('output', 'bootloader', 'partition', 'ota-data', 'app', 'map'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    try:
        path = build(args.output, args.bootloader, args.partition, args.ota_data, args.app, args.map)
        print(f'UNSIGNED DEVELOPMENT ONLY: {path}')
    except (OSError, ManifestError, ValueError) as exc:
        print('INVALID: ' + str(exc))
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
