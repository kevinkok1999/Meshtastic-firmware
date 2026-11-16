#!/usr/bin/env python3
"""Offline, read-only V30 T-Deck Plus install-manifest verifier.

This tool validates metadata and local artifact bytes. It never opens a serial,
USB, or hardware device and never writes flash.
"""
from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path
from typing import Any

FLASH_SIZE = 0x1000000
BOARD = "lilygo-t-deck-plus"
CHIP = "ESP32-S3"
EXPECTED_OFFSETS = {
    "bootloader": 0x000000,
    "partition": 0x008000,
    "ota_data": 0x00E000,
    "app0": 0x010000,
    "app1": 0x3F0000,
}
APP_PARTITION_SIZE = 0x3E0000
APP_HEADROOM = 0x40000
MAP_OFFSET = 0x7D0000
MAP_SIZE = 0x4C0000
SPIFFS_OFFSET = 0xC90000
SPIFFS_SIZE = 0x360000
MAX_PACK_METADATA = 0x380000  # 3.5 MiB


class ManifestError(ValueError):
    pass


def _int(value: Any, field: str) -> int:
    if isinstance(value, bool):
        raise ManifestError(f"{field} must be an integer")
    try:
        return int(value, 0) if isinstance(value, str) else int(value)
    except (TypeError, ValueError):
        raise ManifestError(f"{field} must be an integer") from None


def _artifact_path(root: Path, name: str) -> Path:
    if not isinstance(name, str) or not name or Path(name).is_absolute():
        raise ManifestError("artifact path must be relative")
    candidate = (root / name).resolve()
    try:
        candidate.relative_to(root.resolve())
    except ValueError:
        raise ManifestError(f"artifact path escapes artifact root: {name}") from None
    return candidate


def _sha256(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            size += len(chunk)
            digest.update(chunk)
    return size, digest.hexdigest()


def validate_manifest(manifest: dict[str, Any], artifact_root: str | Path = ".") -> list[str]:
    if not isinstance(manifest, dict):
        raise ManifestError("manifest must be a JSON object")
    if manifest.get("board") != BOARD:
        raise ManifestError("unsupported board; expected ESP32-S3 T-Deck Plus")
    if manifest.get("chip") != CHIP:
        raise ManifestError("unsupported chip; expected ESP32-S3")
    if _int(manifest.get("flash_size_bytes"), "flash_size_bytes") != FLASH_SIZE:
        raise ManifestError("flash_size_bytes must be 16 MiB")
    if manifest.get("production") is True and manifest.get("signature_verified") is not True:
        raise ManifestError("production manifest requires independently verified signature; hashes are not signatures")
    root = Path(artifact_root).resolve()
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        raise ManifestError("artifacts must be a non-empty list")
    by_kind: dict[str, dict[str, Any]] = {}
    intervals: list[tuple[int, int, str]] = []
    for item in artifacts:
        if not isinstance(item, dict):
            raise ManifestError("each artifact must be an object")
        kind = item.get("kind")
        if not isinstance(kind, str) or kind in by_kind:
            raise ManifestError(f"duplicate or missing artifact kind: {kind!r}")
        offset = _int(item.get("offset"), f"{kind}.offset")
        declared_size = _int(item.get("size"), f"{kind}.size")
        if offset < 0 or declared_size < 0 or offset + declared_size > FLASH_SIZE:
            raise ManifestError(f"{kind} is outside 16 MiB flash")
        if kind in EXPECTED_OFFSETS and offset != EXPECTED_OFFSETS[kind]:
            raise ManifestError(f"{kind} offset must be 0x{EXPECTED_OFFSETS[kind]:X}")
        if kind == "ota_data" and declared_size != 0x2000:
            raise ManifestError("ota_data must be exactly 8192 bytes at 0xE000")
        if kind in ("app0", "app1") and declared_size > APP_PARTITION_SIZE - APP_HEADROOM:
            raise ManifestError(f"{kind} exceeds application size minus 256 KiB headroom")
        if kind == "map":
            if offset != MAP_OFFSET or declared_size != MAP_SIZE:
                raise ManifestError("map must fit the 0x7D0000..0xC8FFFF 4.75 MiB partition")
        if kind == "spiffs" and (offset != SPIFFS_OFFSET or declared_size > SPIFFS_SIZE):
            raise ManifestError("spiffs must fit the preserved 0xC90000 partition")
        name = item.get("path", item.get("file"))
        path = _artifact_path(root, name)
        if not path.is_file():
            raise ManifestError(f"artifact does not exist: {name}")
        actual_size, actual_hash = _sha256(path)
        if actual_size != declared_size:
            raise ManifestError(f"{kind} size mismatch: manifest {declared_size}, file {actual_size}")
        expected_hash = item.get("sha256")
        if not isinstance(expected_hash, str) or len(expected_hash) != 64 or expected_hash.lower() != actual_hash:
            raise ManifestError(f"{kind} sha256 mismatch")
        end = offset + declared_size
        for other_start, other_end, other_kind in intervals:
            if offset < other_end and other_start < end:
                raise ManifestError(f"{kind} overlaps {other_kind}")
        intervals.append((offset, end, kind))
        by_kind[kind] = item
    missing = set(EXPECTED_OFFSETS) - set(by_kind)
    if missing:
        raise ManifestError("missing required artifacts: " + ", ".join(sorted(missing)))
    metadata = _int(manifest.get("pack_metadata_size", 0), "pack_metadata_size")
    if metadata < 0 or metadata > MAX_PACK_METADATA:
        raise ManifestError("pack metadata exceeds 3.5 MiB")
    return [f"validated {len(artifacts)} artifacts", "offline/read-only validation", "hashes checked; signature status is independent"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--artifact-root", type=Path, default=None)
    args = parser.parse_args()
    try:
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        root = args.artifact_root if args.artifact_root is not None else args.manifest.parent
        for line in validate_manifest(manifest, root):
            print(line)
    except (OSError, json.JSONDecodeError, ManifestError) as exc:
        print(f"INVALID: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
