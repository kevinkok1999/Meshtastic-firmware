#!/usr/bin/env python3
"""Negative mutation tests for V29 bundle; invalid images must fail closed."""
import json
import pathlib
import shutil
import sys
import tempfile

from test_flash_bundle import validate


def rejects(folder, description):
    try:
        validate(folder)
    except (AssertionError, ValueError, KeyError):
        return
    raise AssertionError("V29 corrupt flash bundle was accepted: " + description)


def main(src):
    validate(src)
    with tempfile.TemporaryDirectory() as tmp:
        dst = pathlib.Path(tmp)
        for f in pathlib.Path(src).iterdir():
            if f.is_file():
                shutil.copyfile(f, dst / f.name)
        layout_path = dst / "flash-layout.json"
        original = json.loads(layout_path.read_text())
        broken = json.loads(json.dumps(original))
        broken["files"][0]["sha256"] = "0" * 64
        layout_path.write_text(json.dumps(broken))
        rejects(dst, "forged digest")
        layout_path.write_text(json.dumps(original))
        broken = json.loads(json.dumps(original))
        broken["files"][1]["offset"] = 0x10000
        layout_path.write_text(json.dumps(broken))
        rejects(dst, "wrong segment address")
        layout_path.write_text(json.dumps(original))
        file = next(x["file"] for x in original["files"] if x["role"] == "app")
        bin_path = dst / file
        tampered = bytearray(bin_path.read_bytes())
        tampered[0] ^= 0xFF
        bin_path.write_bytes(tampered)
        rejects(dst, "modified application")
    print("PASS: V29 firmware metadata and image corruption rejected")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_flash_bundle_negative.py <unpacked-artifact-dir>")
    main(sys.argv[1])
