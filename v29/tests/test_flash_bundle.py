#!/usr/bin/env python3
"""Validate a V29 flash bundle extracted from the CI artifact ZIP.

Usage: python v29/tests/test_flash_bundle.py path/to/v29-release
No device is flashed. Rejects missing, altered, overlapping or out-of-range images.
"""
import hashlib
import json
import pathlib
import sys

def validate(folder):
    folder = pathlib.Path(folder)
    layout = json.loads((folder / "flash-layout.json").read_text())
    assert layout["board"] == "lilygo-t-deck-plus"
    assert layout["flashSizeBytes"] == 16 * 1024 * 1024
    entries = layout["files"]
    assert {item["role"] for item in entries} == {"bootloader", "partitions", "app"}
    expected = {"bootloader": 0, "partitions": 0x8000, "app": 0x10000}
    ranges = []
    for item in entries:
        assert item["offset"] == expected[item["role"]], item
        name = item["file"]
        assert pathlib.Path(name).name == name
        data = (folder / name).read_bytes()
        assert len(data) == item["size"], name
        assert hashlib.sha256(data).hexdigest() == item["sha256"], name
        start, end = item["offset"], item["offset"] + len(data)
        assert 0 <= start < end <= layout["flashSizeBytes"], name
        ranges.append((start, end, name))
    ranges.sort()
    for a, b in zip(ranges, ranges[1:]):
        assert a[1] <= b[0], (a[2], b[2])
    manifest = json.loads((folder / "manifest.json").read_text())
    app = next(x for x in entries if x["role"] == "app")
    assert manifest["sha256"] == app["sha256"]
    assert manifest["size"] == app["size"]
    assert manifest["sourceCommit"] == layout["sourceCommit"]
    print("PASS: V29 artifact checksums, offsets, overlaps, manifest provenance")
    print("NOT VERIFIED: physical boot, RF, encryption, USB flashing, persistence")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_flash_bundle.py <artifact-extract-folder>")
    validate(sys.argv[1])
