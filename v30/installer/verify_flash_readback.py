#!/usr/bin/env python3
"""Compare two local readback snapshots; never open or write a device.

Snapshots contain private flash data. Keep them local and do not upload.
"""
from __future__ import annotations

import argparse
import hashlib
import hmac
import json
from pathlib import Path
import sys

from flash_review_plan import make_review
from verify_install_manifest import FLASH_SIZE, ManifestError
from verify_signed_manifest import bounded_read


def check_readback(manifest: Path, signature: Path, public_key: Path, pin: str,
                   before: Path, after: Path, artifact_root: Path | None = None) -> dict:
    plan = make_review(manifest, signature, public_key, pin, artifact_root)
    if before.samefile(after):
        raise ManifestError('before and after snapshots must be separate files')
    old = bounded_read(before, FLASH_SIZE, 'before readback')
    new = bounded_read(after, FLASH_SIZE, 'after readback')
    if len(old) != FLASH_SIZE or len(new) != FLASH_SIZE:
        raise ManifestError('both readbacks must be exactly 16 MiB')
    partition = next(s for s in plan['segments'] if s['kind'] == 'partition')
    start, size = partition['offset'], partition['size']
    if (not hmac.compare_digest(hashlib.sha256(old[start:start+size]).hexdigest(), partition['sha256'])
            or any(b != 255 for b in old[start+size:partition['erase_end']])):
        raise ManifestError('existing partition layout does not match signed layout')
    preserved = []
    for item in plan['preserved_ranges']:
        start, stop = item['start'], item['end']
        if old[start:stop] != new[start:stop]:
            raise ManifestError(f"preserved {item['name']} differs")
        preserved.append(item['name'])
    cursor = 0
    matches = []
    for s in plan['segments']:
        start, size, stop = s['offset'], s['size'], s['erase_end']
        if old[cursor:start] != new[cursor:start]:
            raise ManifestError('bytes outside reviewed erase sectors differ')
        if not hmac.compare_digest(hashlib.sha256(new[start:start+size]).hexdigest(), s['sha256']):
            raise ManifestError(f"{s['kind']} readback digest mismatch")
        if any(b != 255 for b in new[start+size:stop]):
            raise ManifestError(f"{s['kind']} sector padding is not erased")
        matches.append(s['kind'])
        cursor = stop
    if old[cursor:] != new[cursor:]:
        raise ManifestError('bytes outside reviewed erase sectors differ')
    # Only named outcomes leave this process: no raw bytes, user-data digests,
    # device identifiers or pathnames from the snapshots are emitted.
    return {'schema': 1, 'byte_match': True, 'matched_segments': matches,
            'preserved_match': preserved, 'boot_verified': False,
            'release_authorized': False, 'scope': 'offline-readback-comparison-only'}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ('manifest', 'signature', 'public_key'):
        parser.add_argument(field, type=Path)
    parser.add_argument('--public-key-sha256', required=True)
    parser.add_argument('--artifact-root', type=Path)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = check_readback(args.manifest, args.signature, args.public_key,
                                args.public_key_sha256, args.before, args.after, args.artifact_root)
        print(json.dumps(result, sort_keys=True, indent=2))
    except (OSError, ValueError, ManifestError) as exc:
        print('INVALID: ' + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
