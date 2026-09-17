Import("env")

from pathlib import Path
import json


def _parse_offset(value):
    if isinstance(value, int):
        return value
    text = env.subst(str(value)).strip()
    return int(text, 0)


def _resolved_path(value):
    return Path(env.subst(str(value))).expanduser().resolve()


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


def build_xr_flash_bundle(source, target, build_env):
    app_path = Path(str(target[0])).resolve()
    app_offset_raw = build_env.get("ESP32_APP_OFFSET")
    if app_offset_raw is None:
        raise RuntimeError("ESP32_APP_OFFSET is unavailable; refusing to guess flash layout")
    app_offset = _parse_offset(app_offset_raw)

    raw = build_env.Flatten(build_env.get("FLASH_EXTRA_IMAGES", []))
    if len(raw) % 2 != 0:
        raise RuntimeError(f"Unexpected FLASH_EXTRA_IMAGES shape: {raw!r}")

    entries = []
    for index in range(0, len(raw), 2):
        offset = _parse_offset(raw[index])
        path = _resolved_path(raw[index + 1])
        entries.append((offset, path))

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
        resolved.append({
            "offset": offset,
            "path": str(path),
            "source_name": path.name,
            "role": _role_for(path, offset, app_path),
            "size": size,
        })

    previous_end = 0
    for item in resolved:
        if item["offset"] < previous_end:
            raise RuntimeError(f"Overlapping flash region at 0x{item['offset']:x}: {item['path']}")
        previous_end = item["offset"] + item["size"]

    if not resolved or resolved[0]["offset"] != 0:
        raise RuntimeError(
            "Resolved ESP32-S3 flash map does not start at offset 0; refusing to create a misleading full image"
        )

    build_dir = Path(build_env.subst("$BUILD_DIR")).resolve()
    full_path = build_dir / "xr-full-flash.bin"
    map_path = build_dir / "xr-flash-map.json"

    image = bytearray(b"\xff" * previous_end)
    for item in resolved:
        data = Path(item["path"]).read_bytes()
        start = item["offset"]
        image[start:start + len(data)] = data
    full_path.write_bytes(image)

    board = build_env.BoardConfig()
    manifest = {
        "schema": 1,
        "target": "LILYGO T-Deck Plus",
        "environment": build_env.subst("$PIOENV"),
        "mcu": board.get("build.mcu", "esp32s3"),
        "flash_size": board.get("upload.flash_size", None),
        "app_offset": app_offset,
        "images": resolved,
        "full_image": {
            "path": str(full_path),
            "offset": 0,
            "size": full_path.stat().st_size,
        },
    }
    map_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"XR flash map: {map_path}")
    print(f"XR full flash image: {full_path} ({full_path.stat().st_size} bytes)")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", build_xr_flash_bundle)
