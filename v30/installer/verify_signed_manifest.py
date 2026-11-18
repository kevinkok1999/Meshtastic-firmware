#!/usr/bin/env python3
"""Read-only V30 detached Ed25519 manifest verification.

Trust requires an independently supplied public-key fingerprint. This does not
authorize a production release, flash hardware or change partitions.
"""
import argparse
import hashlib
import hmac
import json
import re
import sys
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
from verify_install_manifest import (validate_manifest, ManifestError, MAP_SIZE,
                                     _artifact_path, _int)


MAX_MANIFEST_BYTES = 256 * 1024


def bounded_read(path: Path, limit: int, field: str) -> bytes:
    with path.open("rb") as handle:
        data = handle.read(limit + 1)
    if len(data) > limit:
        raise ManifestError(field + " exceeds byte limit")
    return data


def strict_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ManifestError("duplicate JSON key")
        result[key] = value
    return result


def reject_constant(_):
    raise ManifestError("non-finite JSON number")


def read_authenticated_manifest(manifest_path: Path, signature_path: Path, public_key_path: Path,
                                expected_pin: str) -> dict:
    if not isinstance(expected_pin, str) or re.fullmatch(r"[0-9a-f]{64}", expected_pin) is None:
        raise ManifestError("public key pin must be lowercase SHA256 hex")
    public_key = bounded_read(public_key_path, 32, "Ed25519 public key")
    if len(public_key) != 32:
        raise ManifestError("Ed25519 public key must be exactly 32 raw bytes")
    if not hmac.compare_digest(hashlib.sha256(public_key).hexdigest(), expected_pin):
        raise ManifestError("public key pin mismatch")
    signature = bounded_read(signature_path, 64, "Ed25519 signature")
    if len(signature) != 64:
        raise ManifestError("Ed25519 signature must be exactly 64 raw bytes")
    payload = bounded_read(manifest_path, MAX_MANIFEST_BYTES, "manifest")
    try:
        Ed25519PublicKey.from_public_bytes(public_key).verify(signature, payload)
    except (InvalidSignature, ValueError) as exc:
        raise ManifestError("Ed25519 signature verification failed") from exc
    try:
        manifest = json.loads(payload.decode("utf-8"), object_pairs_hook=strict_object,
                              parse_constant=reject_constant)
    except (UnicodeDecodeError, json.JSONDecodeError, RecursionError) as exc:
        raise ManifestError("manifest is not complete UTF-8 JSON") from exc
    return manifest


def verify_signed_manifest(manifest_path: Path, signature_path: Path, public_key_path: Path,
                           expected_pin: str, artifact_root: Path | None = None) -> list[str]:
    manifest = read_authenticated_manifest(manifest_path, signature_path, public_key_path, expected_pin)
    results = validate_manifest(manifest, artifact_root if artifact_root is not None else manifest_path.parent)
    return results + ["detached Ed25519 signature verified against supplied pin; not release authorization"]


def verified_map_pin(manifest_path: Path, signature_path: Path, public_key_path: Path,
                     expected_pin: str, artifact_root: Path | None = None) -> str:
    """Return the logical MMN1 SHA256 only from authenticated, validated bytes."""
    manifest = read_authenticated_manifest(manifest_path, signature_path, public_key_path, expected_pin)
    root = artifact_root if artifact_root is not None else manifest_path.parent
    validate_manifest(manifest, root)
    if manifest.get('install_mode') != 'full-v30':
        raise ManifestError('map pin requires full-v30 install mode')
    item = next(x for x in manifest['artifacts'] if x['kind'] == 'map')
    path = _artifact_path(root, item.get('path', item.get('file')))
    # Recheck the full signed image digest in the same snapshot that yields
    # the logical pack pin. The file may have been replaced after validation.
    image = bounded_read(path, MAP_SIZE, 'map image')
    if len(image) != MAP_SIZE or not hmac.compare_digest(hashlib.sha256(image).hexdigest(), item['sha256'].lower()):
        raise ManifestError('map image changed during pin derivation')
    pack_size = _int(item['pack_size'], 'map.pack_size')
    return hashlib.sha256(image[:pack_size]).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("signature", type=Path)
    parser.add_argument("public_key", type=Path)
    parser.add_argument("--public-key-sha256", required=True)
    parser.add_argument("--artifact-root", type=Path)
    parser.add_argument('--print-map-pin', action='store_true', help='print only authenticated logical map SHA256')
    args = parser.parse_args()
    try:
        if args.print_map_pin:
            print(verified_map_pin(args.manifest, args.signature, args.public_key,
                                   args.public_key_sha256, args.artifact_root))
            return 0
        for result in verify_signed_manifest(args.manifest, args.signature, args.public_key,
                                             args.public_key_sha256, args.artifact_root):
            print(result)
    except (OSError, ManifestError, ValueError) as exc:
        print("INVALID: " + str(exc), file=sys.stderr if args.print_map_pin else sys.stdout)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
