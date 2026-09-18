#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): PlatformIO/SCons injects Import and env

import csv
import hashlib
import json
from pathlib import Path

Import("env")

EXPECTED_FLASH_BYTES = 0x1000000
EXPECTED_APP_OFFSET = 0x10000
EXPECTED_FILESYSTEM_OFFSET = 0xC90000
EXPECTED_FILESYSTEM_SIZE = 0x360000
EXPECTED_COREDUMP_OFFSET = 0xFF0000

EXPECTED_PARTITIONS = {
    "nvs": (0x9000, 0x5000),
    "otadata": (0xE000, 0x2000),
    "app0": (0x10000, 0x640000),
    "app1": (0x650000, 0x640000),
    "spiffs": (0xC90000, 0x360000),
    "coredump": (0xFF0000, 0x10000),
}


def _parse_offset(value):
    if isinstance(value, int):
        return value
    text = env.subst(str(value)).strip()
    return int(text, 0)


def _resolved_path(value):
    return Path(env.subst(str(value))).expanduser().resolve()


def _partition_csv(build_env):
    configured = build_env.GetProjectOption("board_build.partitions", None)
    if not configured:
        configured = build_env.BoardConfig().get("build.partitions", None)
    if not configured:
        raise RuntimeError("board_build.partitions is unavailable")

    path = Path(build_env.subst("$PROJECT_DIR")) / str(configured)
    path = path.resolve()
    if not path.is_file():
        raise RuntimeError(f"Partition CSV does not exist: {path}")
    return path


def _read_partitions(path):
    partitions = {}
    with path.open("r", encoding="utf-8") as handle:
        for raw in handle:
            stripped = raw.strip()
            if not stripped or stripped.startswith("#"):
                continue
            row = next(csv.reader([raw], skipinitialspace=True))
            if len(row) < 5:
                raise RuntimeError(f"Invalid partition row: {raw.rstrip()}")
            name = row[0].strip()
            offset = int(row[3].strip(), 0)
            size = int(row[4].strip(), 0)
            partitions[name] = {"offset": offset, "size": size}
    return partitions


def _validate_partition_layout(partitions):
    for name, expected in EXPECTED_PARTITIONS.items():
        actual = partitions.get(name)
        if actual is None:
            raise RuntimeError(f"Required partition '{name}' is missing")
        if (actual["offset"], actual["size"]) != expected:
            raise RuntimeError(
                f"Partition '{name}' changed: "
                f"expected offset=0x{expected[0]:x},size=0x{expected[1]:x}; "
                f"got offset=0x{actual['offset']:x},size=0x{actual['size']:x}"
            )

    if partitions["coredump"]["offset"] + partitions["coredump"]["size"] != EXPECTED_FLASH_BYTES:
        raise RuntimeError("Partition layout no longer fills exactly 16 MiB")


def _role_for(path, offset, app_path):
    if path == app_path:
        return "application"
    name = path.name.lower()
    if "bootloader" in name:
        return "bootloader"
    if "partition" in name:
        return "partitions"
    if "boot_app" in name or "ota_data" in name:
        return "ota_data_initial"
    return f"image_{offset:08x}"


def _sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _validate_binary_markers(image):
    if len(image) <= EXPECTED_FILESYSTEM_OFFSET:
        raise RuntimeError("Full image is too small to contain the filesystem")
    if image[0] != 0xE9:
        raise RuntimeError("Bootloader image magic at 0x000000 is not ESP32 0xE9")
    if image[0x8000:0x8002] != b"\xaa\x50":
        raise RuntimeError("Partition-table signature at 0x008000 is not 0xAA50")
    if image[EXPECTED_APP_OFFSET] != 0xE9:
        raise RuntimeError("Application image magic at 0x010000 is not ESP32 0xE9")

    fs = image[EXPECTED_FILESYSTEM_OFFSET:EXPECTED_COREDUMP_OFFSET]
    if not fs or all(byte == 0xFF for byte in fs):
        raise RuntimeError("Filesystem region is empty/erased; refusing incomplete full-flash image")


def build_xr_flash_bundle(source, target, build_env):
    app_path = Path(build_env.subst("$BUILD_DIR")) / f"{build_env.subst('$PROGNAME')}.bin"
    app_path = app_path.resolve()
    if not app_path.is_file():
        print(f"XR bundle deferred until application exists: {app_path}")
        return

    app_offset_raw = build_env.get("ESP32_APP_OFFSET")
    if app_offset_raw is None:
        raise RuntimeError("ESP32_APP_OFFSET is unavailable; refusing to guess flash layout")
    app_offset = _parse_offset(app_offset_raw)
    if app_offset != EXPECTED_APP_OFFSET:
        raise RuntimeError(f"Unexpected app offset 0x{app_offset:x}; expected 0x{EXPECTED_APP_OFFSET:x}")

    partitions_path = _partition_csv(build_env)
    partitions = _read_partitions(partitions_path)
    _validate_partition_layout(partitions)

    build_dir = Path(build_env.subst("$BUILD_DIR")).resolve()
    filesystem_path = build_dir / "littlefs.bin"
    if not filesystem_path.is_file():
        print("XR full-flash bundle deferred: run buildfs to produce littlefs.bin")
        return

    filesystem_size = filesystem_path.stat().st_size
    if filesystem_size <= 0 or filesystem_size > EXPECTED_FILESYSTEM_SIZE:
        raise RuntimeError(
            f"Invalid LittleFS image size {filesystem_size}; partition limit is {EXPECTED_FILESYSTEM_SIZE}"
        )

    raw = build_env.Flatten(build_env.get("FLASH_EXTRA_IMAGES", []))
    if len(raw) % 2 != 0:
        raise RuntimeError(f"Unexpected FLASH_EXTRA_IMAGES shape: {raw!r}")

    entries = []
    for index in range(0, len(raw), 2):
        entries.append((_parse_offset(raw[index]), _resolved_path(raw[index + 1])))
    entries.append((app_offset, app_path))

    unique = []
    seen = set()
    for offset, path in entries:
        key = (offset, str(path))
        if key not in seen:
            unique.append((offset, path))
            seen.add(key)

    resolved = []
    for offset, path in sorted(unique, key=lambda item: item[0]):
        if not path.is_file():
            raise RuntimeError(f"Flash image does not exist: {path}")
        size = path.stat().st_size
        if size <= 0:
            raise RuntimeError(f"Flash image is empty: {path}")
        resolved.append(
            {
                "offset": offset,
                "path": str(path),
                "source_name": path.name,
                "role": _role_for(path, offset, app_path),
                "size": size,
            }
        )

    resolved.append(
        {
            "offset": EXPECTED_FILESYSTEM_OFFSET,
            "path": str(filesystem_path),
            "source_name": filesystem_path.name,
            "role": "littlefs",
            "size": filesystem_size,
            "partition_size": EXPECTED_FILESYSTEM_SIZE,
        }
    )
    resolved.sort(key=lambda item: item["offset"])

    previous_end = 0
    for item in resolved:
        item_end = item["offset"] + item["size"]
        if item["offset"] < previous_end:
            raise RuntimeError(f"Overlapping flash region at 0x{item['offset']:x}: {item['path']}")
        if item_end > EXPECTED_COREDUMP_OFFSET:
            raise RuntimeError(
                f"Flash region crosses reserved coredump boundary: {item['path']} ends at 0x{item_end:x}"
            )
        previous_end = item_end

    if not resolved or resolved[0]["offset"] != 0:
        raise RuntimeError("Resolved ESP32-S3 flash map does not start at offset 0")

    # The first-install image intentionally ends at the coredump partition.
    # eraseAll=true erases the final 64 KiB coredump area before flashing.
    image = bytearray(b"\xff" * EXPECTED_COREDUMP_OFFSET)
    for item in resolved:
        data = Path(item["path"]).read_bytes()
        start = item["offset"]
        image[start:start + len(data)] = data

    _validate_binary_markers(image)

    full_path = build_dir / "xr-full-flash.bin"
    map_path = build_dir / "xr-flash-map.json"
    full_path.write_bytes(image)

    if full_path.stat().st_size != EXPECTED_COREDUMP_OFFSET:
        raise RuntimeError(
            f"Full image size mismatch: {full_path.stat().st_size} != {EXPECTED_COREDUMP_OFFSET}"
        )

    board = build_env.BoardConfig()
    manifest = {
        "schema": 2,
        "target": "LILYGO T-Deck Plus",
        "environment": build_env.subst("$PIOENV"),
        "mcu": board.get("build.mcu", "esp32s3"),
        "flash_size_bytes": EXPECTED_FLASH_BYTES,
        "app_offset": app_offset,
        "filesystem_offset": EXPECTED_FILESYSTEM_OFFSET,
        "filesystem_size": EXPECTED_FILESYSTEM_SIZE,
        "coredump_offset": EXPECTED_COREDUMP_OFFSET,
        "partition_csv": str(partitions_path),
        "images": resolved,
        "full_image": {
            "path": str(full_path),
            "offset": 0,
            "size": full_path.stat().st_size,
            "sha256": _sha256(full_path),
        },
    }
    map_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"XR flash map: {map_path}")
    print(
        f"XR full flash image: {full_path} "
        f"({full_path.stat().st_size} bytes, sha256={manifest['full_image']['sha256']})"
    )


# The firmware post-action can run before the filesystem exists. The buildfs
# post-action is authoritative and overwrites the bundle once LittleFS is built.
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", build_xr_flash_bundle)
env.AddPostAction("$BUILD_DIR/littlefs.bin", build_xr_flash_bundle)
