#!/usr/bin/env python3
"""Offline, read-only V30 T-Deck Plus install-manifest verifier.

This tool validates metadata and local artifact bytes. It never opens a serial,
USB, or hardware device and never writes flash.
"""
from __future__ import annotations
import argparse, hashlib, json, struct, zlib
from pathlib import Path
from typing import Any

FLASH_SIZE = 0x1000000
BOARD = "lilygo-t-deck-plus"
CHIP = "ESP32-S3"
ALLOWED_KINDS = {"bootloader", "partition", "ota_data", "app0", "app1", "map"}
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
MAX_PACK_SIZE = 0x380000  # 3.5 MiB logical pack, excluding image padding
MAX_PACK_METADATA = 65_536  # native NanoMapPack metadata bound
MAX_DECODED_MAP_BODY = 4 * 1024 * 1024  # native streaming reader work bound
MAP_MAGIC = b"MMN1"
MAP_FORMAT_VERSION = 1
MAP_HEADER_SIZE = 10
MAP_CRC_SIZE = 4
# Exact immutable WadaMesh T-Deck Plus partition layout; source
# 6fe6b9f.../variants/lilygo_tdeck/partitions_tdeck_touch.csv.
PARTITION_LAYOUT = (
    ("nvs", 0x01, 0x02, 0x9000, 0x5000),
    ("otadata", 0x01, 0x00, 0xE000, 0x2000),
    ("app0", 0x00, 0x10, 0x10000, 0x3E0000),
    ("app1", 0x00, 0x11, 0x3F0000, 0x3E0000),
    ("tiles", 0x01, 0x83, 0x7D0000, 0x4C0000),
    ("spiffs", 0x01, 0x82, 0xC90000, 0x360000),
    ("coredump", 0x01, 0x03, 0xFF0000, 0x10000),
)


class ManifestError(ValueError):
    pass


def _int(value: Any, field: str) -> int:
    if isinstance(value, bool) or (isinstance(value, float) and not value.is_integer()) or not isinstance(value, (str, int, float)):
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



def _validate_partition(path: Path, expected_hash: str) -> None:
    with path.open("rb") as handle:
        image = handle.read(0x1001)
    if len(image) not in (0xC00, 0x1000):
        raise ManifestError("partition binary must occupy 0xC00 or 0x1000 bytes")
    if hashlib.sha256(image).hexdigest() != expected_hash:
        raise ManifestError("partition sha256 changed during validation")
    entries = b"".join(struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size,
                                   label.encode("ascii"), 0)
                       for label, kind, subtype, offset, size in PARTITION_LAYOUT)
    if image[:len(entries)] != entries:
        raise ManifestError("partition binary layout differs from pinned T-Deck Plus layout")
    # MD5 is the ESP-IDF table format checksum, not a trust/authentication primitive.
    checksum = b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(entries, usedforsecurity=False).digest()
    if image[len(entries):len(entries) + 32] != checksum:
        raise ManifestError("partition table checksum mismatch")
    if any(value != 255 for value in image[len(entries) + 32:]):
        raise ManifestError("partition table padding must be erased (0xFF)")


def _validate_map(path: Path, item: dict[str, Any], manifest: dict[str, Any]) -> None:
    pack_size = _int(item.get("pack_size"), "map.pack_size")
    if not MAP_HEADER_SIZE + MAP_CRC_SIZE < pack_size <= MAX_PACK_SIZE:
        raise ManifestError("map.pack_size must include header, metadata, payload and CRC32 within 3.5 MiB")
    if type(item.get("padding_byte")) is not int or item["padding_byte"] != 255:
        raise ManifestError("map.padding_byte must explicitly be 255 (0xFF)")
    metadata_size = _int(manifest.get("pack_metadata_size"), "pack_metadata_size")
    if not 0 < metadata_size <= MAX_PACK_METADATA:
        raise ManifestError("map metadata must be 1..65536 bytes")
    # The complete fixed-size image has already passed size/hash checks.
    # Read a bounded image snapshot; neither the pack nor padding is inferred.
    with path.open("rb") as handle:
        image = handle.read(MAP_SIZE + 1)
    if len(image) != MAP_SIZE:
        raise ManifestError("map image size changed during validation")
    if hashlib.sha256(image).hexdigest() != item["sha256"].lower():
        raise ManifestError("map sha256 changed during validation")
    pack = image[:pack_size]
    if pack[:4] != MAP_MAGIC:
        raise ManifestError("map pack magic must be MMN1")
    version, actual_metadata_size = struct.unpack_from("<HI", pack, 4)
    if version != MAP_FORMAT_VERSION:
        raise ManifestError("map pack version must be 1")
    if actual_metadata_size != metadata_size:
        raise ManifestError("map metadata size mismatch between manifest and pack header")
    if MAP_HEADER_SIZE + actual_metadata_size >= pack_size - MAP_CRC_SIZE:
        raise ManifestError("map metadata extends beyond pack payload")
    expected_crc = struct.unpack_from("<I", pack, pack_size - MAP_CRC_SIZE)[0]
    if zlib.crc32(pack[:-MAP_CRC_SIZE]) & 0xffffffff != expected_crc:
        raise ManifestError("map pack CRC32 mismatch")
    try:
        metadata = json.loads(pack[MAP_HEADER_SIZE:MAP_HEADER_SIZE + actual_metadata_size].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError, RecursionError) as exc:
        raise ManifestError("map metadata must be complete UTF-8 JSON") from exc
    if not isinstance(metadata, dict) or type(metadata.get("version")) is not int or metadata["version"] != version:
        raise ManifestError("map metadata version must match pack header version")
    try:
        stream = zlib.decompressobj()
        decoded = stream.decompress(pack[MAP_HEADER_SIZE + actual_metadata_size:-MAP_CRC_SIZE],
                                    MAX_DECODED_MAP_BODY + 1)
    except zlib.error as exc:
        raise ManifestError("map zlib stream invalid") from exc
    if len(decoded) > MAX_DECODED_MAP_BODY or stream.unconsumed_tail:
        raise ManifestError("map decoded body exceeds reader limit")
    if not stream.eof or stream.unused_data or not decoded:
        raise ManifestError("map zlib stream must terminate exactly once without trailing data")
    if any(value != 255 for value in image[pack_size:]):
        raise ManifestError("map padding must contain only 0xFF bytes")
    # CRC32 and SHA256 detect inconsistent bytes, not a trusted publisher.
    # Production remains rejected until a separate trust anchor is configured.


def validate_manifest(manifest: dict[str, Any], artifact_root: str | Path = ".") -> list[str]:
    if not isinstance(manifest, dict):
        raise ManifestError("manifest must be a JSON object")
    if manifest.get("board") != BOARD:
        raise ManifestError("unsupported board; expected ESP32-S3 T-Deck Plus")
    if manifest.get("chip") != CHIP:
        raise ManifestError("unsupported chip; expected ESP32-S3")
    if _int(manifest.get("flash_size_bytes"), "flash_size_bytes") != FLASH_SIZE:
        raise ManifestError("flash_size_bytes must be 16 MiB")
    if manifest.get("production") is not False:
        raise ManifestError("production must be explicitly false; release authorization is not configured")
    root = Path(artifact_root).resolve()
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        raise ManifestError("artifacts must be a non-empty list")
    mode = manifest.get("install_mode", "initial")
    if mode not in ("initial", "full-v30"):
        raise ManifestError("install_mode must be initial or full-v30")
    safe_mode = manifest.get("safe_mode") is True
    by_kind: dict[str, dict[str, Any]] = {}
    intervals: list[tuple[int, int, str]] = []
    for item in artifacts:
        if not isinstance(item, dict):
            raise ManifestError("each artifact must be an object")
        kind = item.get("kind")
        if not isinstance(kind, str) or kind in by_kind:
            raise ManifestError(f"duplicate or missing artifact kind: {kind!r}")
        if kind not in ALLOWED_KINDS:
            raise ManifestError(f"artifact kind is not allowlisted: {kind}")
        if kind == "app1" and not safe_mode:
            raise ManifestError("app1 requires explicit safe_mode")
        offset = _int(item.get("offset"), f"{kind}.offset")
        declared_size = _int(item.get("size"), f"{kind}.size")
        if offset < 0 or declared_size <= 0 or offset + declared_size > FLASH_SIZE:
            raise ManifestError(f"{kind} is outside 16 MiB flash")
        if kind == "bootloader" and offset >= 0x8000:
            raise ManifestError("bootloader must remain below partition table at 0x8000")
        if kind == "partition" and (offset != 0x8000 or declared_size > 0x1000):
            raise ManifestError("partition table must be at 0x8000 and no larger than 0x1000")
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
        if kind == "map":
            _validate_map(path, item, manifest)
        if kind == "partition":
            _validate_partition(path, actual_hash)
        end = offset + declared_size
        for other_start, other_end, other_kind in intervals:
            if offset < other_end and other_start < end:
                raise ManifestError(f"{kind} overlaps {other_kind}")
        intervals.append((offset, end, kind))
        by_kind[kind] = item
    required = {"bootloader", "partition", "ota_data", "app0"}
    if mode == "full-v30":
        required.add("map")
    missing = required - set(by_kind)
    if missing:
        raise ManifestError("missing required artifacts: " + ", ".join(sorted(missing)))
    if "map" in by_kind and "pack_metadata_size" not in manifest:
        raise ManifestError("pack_metadata_size is required when a map is present")
    metadata = _int(manifest.get("pack_metadata_size", 0), "pack_metadata_size")
    if metadata < 0 or metadata > MAX_PACK_METADATA:
        raise ManifestError("pack metadata exceeds 65536 bytes")
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
