#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path

FLASH_BYTES = 0x1000000
FULL_BYTES = 0xFF0000
PARTITION_OFFSET = 0x8000
APP_OFFSET = 0x10000
APP_SLOT_BYTES = 0x640000
FILESYSTEM_OFFSET = 0xC90000
FILESYSTEM_BYTES = 0x360000

def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def require_file(path: Path) -> Path:
    if not path.is_file() or path.stat().st_size <= 0:
        raise SystemExit(f"required release file missing/empty: {path}")
    return path

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("release_dir")
    parser.add_argument("--full-name", required=True)
    parser.add_argument("--app-name", required=True)
    args = parser.parse_args()

    release = Path(args.release_dir)
    full_path = require_file(release / args.full_name)
    app_path = require_file(release / args.app_name)
    fs_path = require_file(release / "littlefs.bin")
    partitions_path = require_file(release / "partitions.bin")
    flash_map_path = require_file(release / "xr-flash-map.json")
    esp_manifest_path = require_file(release / "esp-web-tools-manifest.json")
    product_manifest_path = require_file(release / "manifest.json")
    compatibility_manifest_path = require_file(release / "meshoffgrid-manifest.json")

    full = full_path.read_bytes()
    app = app_path.read_bytes()
    fs = fs_path.read_bytes()
    partitions = partitions_path.read_bytes()

    if len(full) != FULL_BYTES:
        raise SystemExit(f"full image size {len(full)} != expected {FULL_BYTES}")
    if len(full) >= FLASH_BYTES:
        raise SystemExit("full image must leave the final coredump partition erased")
    if len(app) > APP_SLOT_BYTES:
        raise SystemExit(f"application {len(app)} bytes exceeds app0 slot {APP_SLOT_BYTES}")
    if len(fs) > FILESYSTEM_BYTES:
        raise SystemExit(f"LittleFS {len(fs)} bytes exceeds filesystem partition {FILESYSTEM_BYTES}")

    if full[0] != 0xE9:
        raise SystemExit("invalid ESP32-S3 bootloader magic at 0x000000")
    if full[PARTITION_OFFSET:PARTITION_OFFSET + 2] != b"\xaa\x50":
        raise SystemExit("invalid partition table signature at 0x008000")
    if full[APP_OFFSET] != 0xE9:
        raise SystemExit("invalid app image magic at 0x010000")
    if app[0] != 0xE9:
        raise SystemExit("standalone application image has invalid ESP32 image magic")
    if partitions[:2] != b"\xaa\x50":
        raise SystemExit("standalone partitions.bin has invalid signature")

    if full[APP_OFFSET:APP_OFFSET + len(app)] != app:
        raise SystemExit("application bytes in full image do not match packaged firmware")
    if full[PARTITION_OFFSET:PARTITION_OFFSET + len(partitions)] != partitions:
        raise SystemExit("partition table bytes in full image do not match packaged partitions.bin")
    if full[FILESYSTEM_OFFSET:FILESYSTEM_OFFSET + len(fs)] != fs:
        raise SystemExit("LittleFS bytes in full image do not match packaged littlefs.bin")
    fs_region = full[FILESYSTEM_OFFSET:FILESYSTEM_OFFSET + FILESYSTEM_BYTES]
    if all(byte == 0xFF for byte in fs_region):
        raise SystemExit("filesystem region is entirely erased")

    flash_map = json.loads(flash_map_path.read_text(encoding="utf-8"))
    if flash_map.get("environment") != "t-deck-ultra-xbee":
        raise SystemExit(f"unexpected production environment: {flash_map.get('environment')}")
    if flash_map.get("flash_size_bytes") != FLASH_BYTES:
        raise SystemExit("flash map does not declare exactly 16 MiB")
    if flash_map.get("flash_mode") != "dio":
        raise SystemExit(f"flash map mode mismatch: {flash_map.get('flash_mode')!r} != 'dio'")
    if flash_map.get("flash_frequency") != "80m":
        raise SystemExit(
            f"flash map frequency mismatch: {flash_map.get('flash_frequency')!r} != '80m'"
        )
    if flash_map.get("app_offset") != APP_OFFSET:
        raise SystemExit("flash map app offset mismatch")
    if flash_map.get("filesystem_offset") != FILESYSTEM_OFFSET:
        raise SystemExit("flash map filesystem offset mismatch")
    if flash_map.get("filesystem_size") != FILESYSTEM_BYTES:
        raise SystemExit("flash map filesystem size mismatch")
    mapped_full = flash_map.get("full_image", {})
    if mapped_full.get("size") != FULL_BYTES:
        raise SystemExit("flash map full-image size mismatch")

    full_hash = sha256_file(full_path)
    if mapped_full.get("sha256") != full_hash:
        raise SystemExit("flash map SHA-256 does not match packaged full image")

    esp_manifest = json.loads(esp_manifest_path.read_text(encoding="utf-8"))
    builds = esp_manifest.get("builds", [])
    if len(builds) != 1 or builds[0].get("chipFamily") != "ESP32-S3":
        raise SystemExit("ESP Web Tools manifest has wrong chip/build count")
    parts = builds[0].get("parts", [])
    if parts != [{"path": args.full_name, "offset": 0}]:
        raise SystemExit("ESP Web Tools manifest does not flash the canonical full image at offset 0")

    product_text = product_manifest_path.read_text(encoding="utf-8")
    compatibility_text = compatibility_manifest_path.read_text(encoding="utf-8")
    if product_text != compatibility_text:
        raise SystemExit("compatibility manifest differs from canonical manifest.json")
    product = json.loads(product_text)
    required = {
        "schema": 1,
        "board": "lilygo-t-deck-plus",
        "chip": "ESP32-S3",
        "flashSizeBytes": FLASH_BYTES,
        "file": args.full_name,
        "offset": 0,
        "sha256": full_hash,
        "size": FULL_BYTES,
        "filesystemOffset": FILESYSTEM_OFFSET,
    }
    for key, expected in required.items():
        if product.get(key) != expected:
            raise SystemExit(f"MeshOffGrid manifest mismatch for {key}: {product.get(key)!r} != {expected!r}")

    if product.get("channel") != "candidate":
        raise SystemExit("unpublished release must remain on candidate channel")
    version = str(product.get("version", ""))
    pieces = version.split("-xr.")
    if len(pieces) != 2 or pieces[0] != "2.8.1" or len(pieces[1]) != 7 or any(ch not in "0123456789abcdef" for ch in pieces[1].lower()):
        raise SystemExit(f"unexpected candidate version format: {version!r}")
    source_commit = str(product.get("sourceCommit", ""))
    if len(source_commit) != 40 or any(ch not in "0123456789abcdef" for ch in source_commit.lower()):
        raise SystemExit("sourceCommit is not a full Git SHA")
    source_run_id = product.get("sourceRunId")
    if not isinstance(source_run_id, int) or source_run_id <= 0:
        raise SystemExit("sourceRunId must be a positive GitHub Actions run id")

    mapped_images = flash_map.get("images", [])
    mapped_roles = {entry.get("role"): entry for entry in mapped_images}
    for role in ("bootloader", "partitions", "application", "filesystem"):
        if role not in mapped_roles:
            raise SystemExit(f"flash map missing required role: {role}")
    if mapped_roles["bootloader"].get("offset") != 0:
        raise SystemExit("flash map bootloader offset mismatch")
    if mapped_roles["partitions"].get("offset") != PARTITION_OFFSET:
        raise SystemExit("flash map partition-table offset mismatch")
    if mapped_roles["application"].get("offset") != APP_OFFSET:
        raise SystemExit("flash map application offset mismatch")
    if mapped_roles["filesystem"].get("offset") != FILESYSTEM_OFFSET:
        raise SystemExit("flash map filesystem offset mismatch")

    sums = {}
    sums_path = require_file(release / "SHA256SUMS.txt")
    for line in sums_path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        digest, name = line.split(maxsplit=1)
        sums[name.lstrip("*")] = digest
    if sums.get(args.full_name) != full_hash:
        raise SystemExit("SHA256SUMS full-image digest mismatch")
    if sums.get(args.app_name) != sha256_file(app_path):
        raise SystemExit("SHA256SUMS app digest mismatch")
    if sums.get("littlefs.bin") != sha256_file(fs_path):
        raise SystemExit("SHA256SUMS LittleFS digest mismatch")

    report = {
        "validated": True,
        "fullImage": args.full_name,
        "fullSize": len(full),
        "fullSha256": full_hash,
        "applicationSize": len(app),
        "filesystemSize": len(fs),
        "flashCapacity": FLASH_BYTES,
        "unusedCoredumpBytes": FLASH_BYTES - FULL_BYTES,
    }
    (release / "VALIDATION.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))

if __name__ == "__main__":
    main()
