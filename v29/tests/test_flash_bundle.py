#!/usr/bin/env python3
"""Validate V29 firmware segment SHA-256, ESP32-S3 flash and partition map.

Usage: python v29/tests/test_flash_bundle.py <unpacked-v29-artifact-directory>
This is NOT evidence of a hardware flash/boot, RF interoperability or security audit.
"""
import hashlib
import json
import pathlib
import re
import struct
import sys

FLASH_BYTES = 16 * 1024 * 1024
OFFSETS = {"bootloader": 0x0000, "partitions": 0x8000, "boot_app0": 0xE000, "app": 0x10000}
OTA_SEED_SHA256 = "f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0"


def parse_partitions(data):
    """Decode standard ESP-IDF 32-byte partition records."""
    if len(data) != 3072:
        raise AssertionError("unexpected ESP32-S3 partition image length")
    result = []
    for pos in range(0, len(data), 32):
        magic, kind, subtype, start, length, label, flags = struct.unpack_from("<HBBII16sI", data, pos)
        if magic == 0xFFFF:
            break
        if magic == 0xEBEB:
            continue
        assert magic == 0x50AA, f"invalid partition magic at {pos:#x}"
        assert length and start % 0x1000 == 0 and start + length <= FLASH_BYTES
        result.append({
            "type": kind,
            "subtype": subtype,
            "start": start,
            "length": length,
            "name": label.split(b"\x00", 1)[0].decode("ascii"),
            "flags": flags,
        })
    assert result, "empty partition map"
    ordered = sorted(result, key=lambda x: x["start"])
    for a, b in zip(ordered, ordered[1:]):
        assert a["start"] + a["length"] <= b["start"], "overlapping partitions"
    return result


def validate(folder):
    folder = pathlib.Path(folder)
    layout = json.loads((folder / "flash-layout.json").read_text(encoding="utf-8"))
    assert layout["board"] == "lilygo-t-deck-plus"
    assert layout["flashSizeBytes"] == FLASH_BYTES
    assert re.fullmatch(r"[0-9a-f]{40}", layout["sourceCommit"]), "missing source provenance"
    entries = layout["files"]
    assert len(entries) == len(OFFSETS)
    assert {x["role"] for x in entries} == set(OFFSETS), "incomplete flash image set"
    ranges = []
    blobs = {}
    for x in entries:
        role, name = x["role"], x["file"]
        assert pathlib.Path(name).name == name and name.endswith(".bin"), "unsafe asset name"
        assert x["offset"] == OFFSETS[role], "unexpected flash offset"
        data = (folder / name).read_bytes()
        assert data and len(data) == x["size"], "incorrect binary length"
        assert hashlib.sha256(data).hexdigest() == x["sha256"], "integrity failure"
        start, end = x["offset"], x["offset"] + len(data)
        assert 0 <= start < end <= FLASH_BYTES
        ranges.append((start, end, role))
        blobs[role] = data
    ranges.sort()
    for a, b in zip(ranges, ranges[1:]):
        assert a[1] <= b[0], "flash image collision"

    assert blobs["bootloader"][:1] == b"\xe9", "invalid ESP32-S3 bootloader"
    assert len(blobs["bootloader"]) <= 0x8000, "bootloader overlaps partitions"
    assert blobs["app"][:1] == b"\xe9", "invalid ESP32-S3 application"
    assert len(blobs["boot_app0"]) == 8192 and blobs["boot_app0"][:4] == b"\x01\x00\x00\x00"
    assert hashlib.sha256(blobs["boot_app0"]).hexdigest() == OTA_SEED_SHA256

    parts = parse_partitions(blobs["partitions"])
    app0 = [x for x in parts if x["type"] == 0 and x["subtype"] == 0x10]
    ota = [x for x in parts if x["type"] == 1 and x["subtype"] == 0]
    assert len(app0) == len(ota) == 1, "expected OTA app0 and otadata"
    assert app0[0]["start"] == OFFSETS["app"]
    assert len(blobs["app"]) <= app0[0]["length"], "firmware exceeds app0 partition"
    assert ota[0]["start"] == OFFSETS["boot_app0"] and ota[0]["length"] == 8192
    # Firmware/OTA images must never overwrite data-storage partitions.
    for start, end, role in ranges:
        if role not in ("app", "boot_app0"):
            continue
        for part in parts:
            if role == "app" and part is app0[0]:
                continue
            if role == "boot_app0" and part is ota[0]:
                continue
            assert end <= part["start"] or start >= part["start"] + part["length"], (
                f"{role} overwrites {part['name']}"
            )

    manifest = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["sha256"] == hashlib.sha256(blobs["app"]).hexdigest()
    assert manifest["size"] == len(blobs["app"])
    assert manifest["sourceCommit"] == layout["sourceCommit"]
    assert manifest["board"] == layout["board"]
    assert manifest["version"].startswith("29.")
    print("PASS: V29 four-part flash integrity, partition and OTA layout, provenance")
    print("NOT VERIFIED: physical USB flashing, boot, RF, runtime security, persistence")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_flash_bundle.py <unpacked-artifact-dir>")
    validate(sys.argv[1])
