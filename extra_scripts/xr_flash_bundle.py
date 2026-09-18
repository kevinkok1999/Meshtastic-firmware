#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): PlatformIO/SCons injects Import and env

import csv
import hashlib
import json
import subprocess
from pathlib import Path

Import("env")

EXPECTED_FLASH_BYTES = 16 * 1024 * 1024
EXPECTED_APP_OFFSET = 0x10000
EXPECTED_FILESYSTEM_OFFSET = 0xC90000
EXPECTED_FILESYSTEM_END = 0xFF0000


def _parse_offset(value):
    if isinstance(value, int):
        return value
    text = env.subst(str(value)).strip()
    return int(text, 0)


def _parse_flash_size(value):
    if isinstance(value, int):
        return value

    text = str(value or "").strip().lower().replace(" ", "")
    if not text:
        return 0
    if text.endswith("mb"):
        return int(text[:-2], 10) * 1024 * 1024
    if text.endswith("m"):
        return int(text[:-1], 10) * 1024 * 1024
    if text.endswith("kb"):
        return int(text[:-2], 10) * 1024
    if text.endswith("k"):
        return int(text[:-1], 10) * 1024
    return int(text, 0)


def _resolved_path(value):
    return Path(env.subst(str(value))).expanduser().resolve()


def _flash_frequency_arg(board):
    raw = str(board.get("build.f_flash", "40000000L")).strip().lower().replace("l", "")
    hz = int(raw, 0)
    if hz % 1_000_000 != 0:
        raise RuntimeError(f"Unsupported non-MHz flash frequency: {hz}")
    return f"{hz // 1_000_000}m"


def _sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


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
    if "littlefs" in name or "spiffs" in name:
        return "filesystem"
    return f"image_{offset:08x}"


def _partition_table_path(build_env):
    board = build_env.BoardConfig()
    configured = board.get("build.partitions", None)
    if not configured:
        raise RuntimeError("Board partition CSV is unavailable; refusing to guess flash layout")

    candidate = Path(str(configured))
    if not candidate.is_absolute():
        candidate = Path(build_env.subst("$PROJECT_DIR")) / candidate
    candidate = candidate.resolve()

    if not candidate.is_file():
        raise RuntimeError(f"Partition CSV does not exist: {candidate}")
    return candidate


def _read_partitions(build_env):
    path = _partition_table_path(build_env)
    partitions = []

    with path.open("r", encoding="utf-8", newline="") as handle:
        for row in csv.reader(handle):
            if not row:
                continue
            first = row[0].strip()
            if not first or first.startswith("#"):
                continue
            if len(row) < 5:
                raise RuntimeError(f"Malformed partition row in {path}: {row!r}")

            name = row[0].strip()
            part_type = row[1].strip()
            subtype = row[2].strip()
            offset = int(row[3].strip(), 0)
            size = int(row[4].strip(), 0)
            if size <= 0:
                raise RuntimeError(f"Invalid partition size for {name}: {size}")

            partitions.append(
                {
                    "name": name,
                    "type": part_type,
                    "subtype": subtype,
                    "offset": offset,
                    "size": size,
                    "end": offset + size,
                }
            )

    if not partitions:
        raise RuntimeError(f"No partitions parsed from {path}")

    for previous, current in zip(partitions, partitions[1:]):
        if current["offset"] < previous["end"]:
            raise RuntimeError(
                f"Partition overlap: {previous['name']} ends at 0x{previous['end']:x}, "
                f"{current['name']} starts at 0x{current['offset']:x}"
            )

    return path, partitions


def _filesystem_partition(partitions):
    for part in partitions:
        subtype = part["subtype"].lower()
        name = part["name"].lower()
        if part["type"].lower() == "data" and (
            subtype in {"spiffs", "littlefs", "fat"} or name in {"spiffs", "littlefs", "filesystem"}
        ):
            return part
    raise RuntimeError("No filesystem partition found in partition CSV")


def _require_role(resolved, role):
    matches = [entry for entry in resolved if entry["role"] == role]
    if len(matches) != 1:
        raise RuntimeError(f"Expected exactly one {role} image, found {len(matches)}")
    return matches[0]


def build_xr_flash_bundle(source, target, build_env):
    app_path = Path(str(target[0])).resolve()
    app_offset_raw = build_env.get("ESP32_APP_OFFSET")
    if app_offset_raw is None:
        raise RuntimeError("ESP32_APP_OFFSET is unavailable; refusing to guess flash layout")
    app_offset = _parse_offset(app_offset_raw)

    board = build_env.BoardConfig()
    flash_bytes = _parse_flash_size(board.get("upload.flash_size", None))
    if flash_bytes != EXPECTED_FLASH_BYTES:
        raise RuntimeError(
            f"Canonical T-Deck Plus XR build requires exactly 16 MiB flash, got {flash_bytes} bytes"
        )

    partition_path, partitions = _read_partitions(build_env)
    filesystem = _filesystem_partition(partitions)

    app0 = next((part for part in partitions if part["name"] == "app0"), None)
    if app0 is None or app0["offset"] != EXPECTED_APP_OFFSET:
        raise RuntimeError(
            f"Expected app0 at 0x{EXPECTED_APP_OFFSET:x}; partition table does not match canonical layout"
        )
    if app_offset != EXPECTED_APP_OFFSET:
        raise RuntimeError(f"Resolved app offset is 0x{app_offset:x}, expected 0x{EXPECTED_APP_OFFSET:x}")
    if filesystem["offset"] != EXPECTED_FILESYSTEM_OFFSET:
        raise RuntimeError(
            f"Filesystem offset is 0x{filesystem['offset']:x}, expected 0x{EXPECTED_FILESYSTEM_OFFSET:x}"
        )
    if filesystem["end"] != EXPECTED_FILESYSTEM_END:
        raise RuntimeError(
            f"Filesystem end is 0x{filesystem['end']:x}, expected 0x{EXPECTED_FILESYSTEM_END:x}"
        )
    if max(part["end"] for part in partitions) > flash_bytes:
        raise RuntimeError("Partition table extends beyond physical 16 MiB flash")

    raw = build_env.Flatten(build_env.get("FLASH_EXTRA_IMAGES", []))
    if len(raw) % 2 != 0:
        raise RuntimeError(f"Unexpected FLASH_EXTRA_IMAGES shape: {raw!r}")

    entries = []
    for index in range(0, len(raw), 2):
        entries.append((_parse_offset(raw[index]), _resolved_path(raw[index + 1])))

    entries.append((app_offset, app_path))

    build_dir = Path(build_env.subst("$BUILD_DIR")).resolve()
    littlefs_path = build_dir / "littlefs.bin"
    if not littlefs_path.is_file() or littlefs_path.stat().st_size <= 0:
        raise RuntimeError(
            f"LittleFS image missing: {littlefs_path}. Run the buildfs target before the firmware build."
        )
    if littlefs_path.stat().st_size > filesystem["size"]:
        raise RuntimeError(
            f"LittleFS image ({littlefs_path.stat().st_size}) exceeds filesystem partition ({filesystem['size']})"
        )
    entries.append((filesystem["offset"], littlefs_path.resolve()))

    unique = []
    seen = set()
    for offset, image_path in entries:
        key = (offset, str(image_path))
        if key not in seen:
            unique.append((offset, image_path))
            seen.add(key)

    resolved = []
    for offset, image_path in sorted(unique, key=lambda item: item[0]):
        if not image_path.is_file():
            raise RuntimeError(f"Flash image does not exist: {image_path}")
        size = image_path.stat().st_size
        if size <= 0:
            raise RuntimeError(f"Flash image is empty: {image_path}")
        end = offset + size
        if end > flash_bytes:
            raise RuntimeError(f"Flash image exceeds 16 MiB at 0x{end:x}: {image_path}")

        resolved.append(
            {
                "offset": offset,
                "path": str(image_path),
                "source_name": image_path.name,
                "role": _role_for(image_path, offset, app_path),
                "size": size,
                "end": end,
                "sha256": _sha256(image_path),
            }
        )

    previous_end = 0
    for item in resolved:
        if item["offset"] < previous_end:
            raise RuntimeError(f"Overlapping flash region at 0x{item['offset']:x}: {item['path']}")
        previous_end = item["end"]

    if not resolved or resolved[0]["offset"] != 0:
        raise RuntimeError(
            "Resolved ESP32-S3 flash map does not start at offset 0; refusing to create a misleading full image"
        )

    bootloader = _require_role(resolved, "bootloader")
    partition_bin = _require_role(resolved, "partitions")
    application = _require_role(resolved, "application")
    filesystem_image = _require_role(resolved, "filesystem")

    if bootloader["offset"] != 0:
        raise RuntimeError(f"Bootloader must start at 0x0, got 0x{bootloader['offset']:x}")
    if partition_bin["offset"] != 0x8000:
        raise RuntimeError(f"Partition table must start at 0x8000, got 0x{partition_bin['offset']:x}")
    if application["offset"] != EXPECTED_APP_OFFSET:
        raise RuntimeError("Application offset mismatch")
    if filesystem_image["offset"] != EXPECTED_FILESYSTEM_OFFSET:
        raise RuntimeError("Filesystem image offset mismatch")

    # Build the canonical first-install image with Espressif's own merge
    # implementation. This is intentionally not a bytearray concatenation:
    # esptool patches the bootloader flash header to the board's actual flash
    # mode/frequency/size and recomputes its digest when required.
    full_path = build_dir / "xr-full-flash.bin"
    map_path = build_dir / "xr-flash-map.json"
    python_exe = _resolved_path(build_env.subst("$PYTHONEXE"))
    uploader = _resolved_path(build_env.subst("$UPLOADER"))
    flash_mode = str(board.get("build.flash_mode", "")).strip().lower()
    flash_freq = _flash_frequency_arg(board)

    if not flash_mode:
        raise RuntimeError("Board flash mode is unavailable; refusing to build canonical image")
    if not uploader.is_file():
        raise RuntimeError(f"PlatformIO esptool uploader not found: {uploader}")

    command = [
        str(python_exe),
        str(uploader),
        "--chip",
        "esp32s3",
        "merge_bin",
        "-o",
        str(full_path),
        "--flash_mode",
        flash_mode,
        "--flash_freq",
        flash_freq,
        "--flash_size",
        "16MB",
    ]
    for item in resolved:
        command.extend([hex(item["offset"]), item["path"]])

    print("XR merge command:", " ".join(command))
    completed = subprocess.run(command, check=False, capture_output=True, text=True)
    if completed.stdout:
        print(completed.stdout)
    if completed.stderr:
        print(completed.stderr)
    if completed.returncode != 0:
        raise RuntimeError(f"esptool merge_bin failed with exit code {completed.returncode}")

    if not full_path.is_file() or full_path.stat().st_size <= 0:
        raise RuntimeError("esptool did not create the canonical full-flash image")

    # Keep the release image deterministic through the end of LittleFS while
    # intentionally leaving the final 64 KiB coredump partition erased. If the
    # filesystem builder emitted a sparse/short image, pad only with erased FF.
    current_size = full_path.stat().st_size
    if current_size > EXPECTED_FILESYSTEM_END:
        raise RuntimeError(
            f"Merged image size {current_size} exceeds canonical 0x{EXPECTED_FILESYSTEM_END:x}"
        )
    if current_size < EXPECTED_FILESYSTEM_END:
        with full_path.open("ab") as handle:
            handle.write(b"\xff" * (EXPECTED_FILESYSTEM_END - current_size))

    if full_path.stat().st_size != EXPECTED_FILESYSTEM_END:
        raise RuntimeError(
            f"Full image size {full_path.stat().st_size} != canonical 0x{EXPECTED_FILESYSTEM_END:x}"
        )

    manifest = {
        "schema": 2,
        "target": "LILYGO T-Deck Plus",
        "environment": build_env.subst("$PIOENV"),
        "mcu": board.get("build.mcu", "esp32s3"),
        "flash_mode": flash_mode,
        "flash_frequency": flash_freq,
        "flash_size_bytes": flash_bytes,
        "partition_csv": str(partition_path),
        "app_offset": app_offset,
        "filesystem_offset": filesystem["offset"],
        "filesystem_size": filesystem["size"],
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


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", build_xr_flash_bundle)
