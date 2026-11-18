#!/usr/bin/env python3
"""Export actual PlatformIO T-Deck Plus bins; offline-only unsigned DEV evidence."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

from verify_install_manifest import (BOARD, CHIP, FLASH_SIZE, APP_PARTITION_SIZE,
                                     APP_HEADROOM, EXPECTED_OFFSETS,
                                     ManifestError, validate_manifest)

ENV = 'LilyGo_TDeck_companion_radio_touch'
UPSTREAM_SHA = '6fe6b9f06332708b6ed0cc78958017d0dcc9e35d'


class ExportError(ValueError):
    pass


def _check_esp_image(path: Path) -> None:
    """Independently check ESP32-S3 magic, bounded segments, checksum and SHA."""
    data = path.read_bytes()
    if len(data) < 64 or data[0] != 0xE9 or int.from_bytes(data[12:14], 'little') != 9:
        raise ExportError(f'{path.name}: invalid ESP32-S3 image header')
    if not 1 <= data[1] <= 16 or data[23] != 1:
        raise ExportError(f'{path.name}: image requires segments and appended SHA256')
    pos, checksum = 24, 0xEF
    for _ in range(data[1]):
        if pos + 8 > len(data):
            raise ExportError(f'{path.name}: truncated segment header')
        _, size = struct.unpack_from('<II', data, pos)
        pos += 8
        if size > len(data) - pos:
            raise ExportError(f'{path.name}: truncated segment body')
        for value in data[pos:pos + size]:
            checksum ^= value
        pos += size
    footer = pos + (15 - pos % 16)
    if footer + 33 != len(data) or data[footer] != checksum:
        raise ExportError(f'{path.name}: invalid checksum/size')
    if hashlib.sha256(data[:footer + 1]).digest() != data[footer + 1:]:
        raise ExportError(f'{path.name}: invalid appended SHA256')


def _image_info(esptool: Path, path: Path) -> str:
    result = subprocess.run([sys.executable, str(esptool), '--chip', 'esp32s3',
                             'image_info', '--version', '2', str(path)],
                            check=True, capture_output=True, text=True, timeout=60)
    return result.stdout + result.stderr


def _validate_esptool_info(info: str) -> None:
    if (not re.search(r'^Chip ID: 9\s*$', info, re.M) or
            not re.search(r'^Checksum: 0x[0-9a-f]+ \(valid\)\s*$', info, re.M) or
            not re.search(r'^Validation hash: [0-9a-f]{64} \(valid\)\s*$', info, re.M)):
        raise ExportError('esptool did not validate chip ID, checksum and image digest')


def _metadata_inputs(metadata: dict, build: Path) -> tuple[dict[str, Path], Path]:
    expected = {'environment': ENV, 'board': 't-deck', 'chip': 'esp32s3', 'flash_size': '16MB'}
    if any(metadata.get(key) != value for key, value in expected.items()):
        raise ExportError('PlatformIO target, board, chip or flash size mismatch')
    try:
        framework = Path(metadata['framework']).resolve(strict=True)
        esptool = Path(metadata['esptool']).resolve(strict=True)
        paths = {'bootloader': build / 'bootloader.bin', 'partition': build / 'partitions.bin',
                 'ota_data': framework / 'tools/partitions/boot_app0.bin', 'app0': build / 'firmware.bin'}
        actual = [(int(offset, 0), Path(path).resolve(strict=True)) for offset, path in metadata['extra_images']]
        expected_images = [(EXPECTED_OFFSETS[kind], paths[kind].resolve(strict=True))
                           for kind in ('bootloader', 'partition', 'ota_data')]
        if (len(actual) != len(expected_images) or set(actual) != set(expected_images) or
                int(metadata['app_offset'], 0) != EXPECTED_OFFSETS['app0'] or
                Path(metadata['app_path']).resolve(strict=True) != paths['app0'] or
                framework.name != 'framework-arduinoespressif32' or esptool.name != 'esptool.py' or
                esptool.parent.name != 'tool-esptoolpy'):
            raise ExportError('PlatformIO flashed image paths or offsets do not match the pinned layout')
    except (KeyError, TypeError, OSError, ValueError) as exc:
        raise ExportError(f'Invalid PlatformIO metadata: {exc}') from exc
    return paths, esptool


def export(metadata_file: Path, build_dir: Path, output: Path, source_sha: str) -> dict:
    if not re.fullmatch('[0-9a-f]{40}', source_sha):
        raise ExportError('exact source commit SHA required')
    if output.exists():
        raise ExportError('output must not preexist')
    build = build_dir.resolve(strict=True)
    if build.name != ENV:
        raise ExportError('wrong build-directory target')
    metadata = json.loads(metadata_file.read_text(encoding='utf-8'))
    paths, esptool = _metadata_inputs(metadata, build)
    for kind, path in paths.items():
        if not path.is_file() or path.is_symlink():
            raise ExportError(f'{kind} input must be an ordinary file')
    if paths['app0'].stat().st_size > APP_PARTITION_SIZE - APP_HEADROOM:
        raise ExportError('insufficient 256 KiB application headroom')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='v30-dev-stage-', dir=output.parent) as temp:
        stage = Path(temp) / 'package'
        stage.mkdir()
        artifacts = []
        for kind, source in paths.items():
            dest = stage / source.name
            shutil.copyfile(source, dest)
            if kind in ('bootloader', 'app0'):
                _check_esp_image(dest)
                info = _image_info(esptool, dest)
                _validate_esptool_info(info)
                (stage / f'{dest.stem}.image-info.txt').write_text(info, encoding='utf-8')
            artifacts.append({'kind': kind, 'offset': EXPECTED_OFFSETS[kind], 'path': dest.name,
                              'size': dest.stat().st_size, 'sha256': hashlib.sha256(dest.read_bytes()).hexdigest()})
        manifest = {'board': BOARD, 'chip': CHIP, 'flash_size_bytes': FLASH_SIZE,
                    'production': False, 'development_only': True, 'signature_status': 'unsigned',
                    'install_mode': 'initial', 'map_included': False, 'physical_flash_tested': False,
                    'source_commit': source_sha, 'upstream_commit': UPSTREAM_SHA,
                    'build_environment': ENV, 'experimental_transport_enabled': False,
                    'app_headroom_bytes': APP_PARTITION_SIZE - paths['app0'].stat().st_size,
                    'artifacts': artifacts}
        validate_manifest(manifest, stage)
        (stage / 'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        (stage / 'pio-flash-metadata.json').write_text(json.dumps(metadata, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        (stage / 'SHA256SUMS').write_text(''.join(f'{a["sha256"]}  {a["path"]}\n' for a in artifacts), encoding='utf-8')
        (stage / 'README.txt').write_text('UNSIGNED V30 DEV CI ARTIFACT ONLY\n'
            'Built for T-Deck Plus ESP32-S3 16 MiB using the pinned WadaMesh reconstruction.\n'
            'No national map, no production authorization, no physical flash or boot test.\n'
            'The manifest records the exact configured offsets, checksums, and provenance.\n'
            'Not for a public installer or end-user deployment.\n', encoding='utf-8')
        stage.rename(output)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-sha', required=True)
    args = parser.parse_args()
    try:
        manifest = export(args.metadata, args.build_dir, args.output, args.source_sha)
    except (ExportError, ManifestError, OSError, subprocess.SubprocessError, json.JSONDecodeError) as exc:
        print(f'INVALID: {exc}', file=sys.stderr)
        return 1
    print(json.dumps(manifest, indent=2, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
