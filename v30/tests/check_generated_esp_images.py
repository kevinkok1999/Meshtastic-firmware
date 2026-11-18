#!/usr/bin/env python3
"""Cross-check genuine generated, unsigned PlatformIO images against esptool."""
import argparse
import hashlib
import sys
from pathlib import Path

from esptool.bin_image import ESP32S3FirmwareImage

sys.path.insert(0, str(Path(__file__).parents[1] / 'installer'))
from verify_install_manifest import _validate_esp_image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build_directory', type=Path)
    args = parser.parse_args()
    for kind, name in (('bootloader', 'bootloader.bin'), ('app0', 'firmware.bin')):
        path = args.build_directory / name
        image = path.read_bytes()
        sha = hashlib.sha256(image).hexdigest()
        _validate_esp_image(path, kind, sha)
        with path.open('rb') as handle:
            upstream = ESP32S3FirmwareImage(handle)
        if upstream.chip_id != 9 or upstream.calculate_checksum() != upstream.checksum:
            raise ValueError(f'esptool mismatch: {kind} chip or checksum')
        if upstream.append_digest and upstream.calc_digest != upstream.stored_digest:
            raise ValueError(f'esptool mismatch: {kind} SHA-256 digest')
        print(f'{kind}: V30 verifier + esptool 4.8.1 agree; {len(image)} bytes, '
              f'{len(upstream.segments)} segments, digest={upstream.append_digest}, sha256={sha}')


if __name__ == '__main__':
    main()
