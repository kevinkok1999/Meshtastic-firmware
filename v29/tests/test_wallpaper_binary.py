#!/usr/bin/env python3
"""Assert that the exact 320x240 RGB565 palm-tree wallpaper is in V29 firmware.

This is a compiled-binary check, not evidence that the LVGL view is visible on
physical T-Deck Plus hardware. Run after the PlatformIO T-Deck build:
  python v29/tests/test_wallpaper_binary.py <firmware.bin> <patched-UITask.cpp>
"""
import hashlib
import pathlib
import re
import sys

WIDTH, HEIGHT, BYTES_PER_PIXEL = 320, 240, 2
ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = ROOT / "overlay/src/ui-touch/V29BeachWallpaper.h"
SOURCE_MARKERS = (
    '#include "V29BeachWallpaper.h"',
    "lv_img_create(",
    "lv_img_set_src(v29_wallpaper, &v29_beach_img)",
    "lv_obj_move_background(v29_wallpaper)",
)


def wallpaper_data():
    header = HEADER.read_text(encoding="utf-8")
    match = re.search(
        r"static const uint8_t v29_beach_pixels\s*\[[^]]+\]\s*=\s*\{(.*?)\};",
        header,
        flags=re.DOTALL,
    )
    assert match, "missing RGB565 wallpaper array"
    image = bytes(int(part, 16) for part in re.findall(r"0x([a-fA-F0-9]{2})", match.group(1)))
    assert len(image) == WIDTH * HEIGHT * BYTES_PER_PIXEL, (
        "wallpaper is not 320x240 RGB565", len(image)
    )
    assert "lv_img_dsc_t v29_beach_img" in header, "missing LVGL image descriptor"
    assert "LV_IMG_CF_TRUE_COLOR" in header, "unexpected LVGL image format"
    assert len(set(image[i:i+2] for i in range(0, len(image), 2))) > 25, (
        "wallpaper looks monochromatic or corrupted"
    )
    return image


def check(firmware, ui):
    image = wallpaper_data()
    ui = pathlib.Path(ui).read_text(encoding="utf-8")
    for marker in SOURCE_MARKERS:
        assert marker in ui, f"wallpaper missing from patched LVGL UI: {marker}"
    binary = pathlib.Path(firmware).read_bytes()
    offset = binary.find(image)
    assert offset >= 0, "full wallpaper pixels are not embedded in compiled firmware.bin"
    assert binary.count(image) == 1, "wallpaper appears multiple times in firmware image"
    print("PASS: 320x240 RGB565 palm-tree wallpaper embedded exactly once in V29 T-Deck Plus app")
    print("Image SHA-256:", hashlib.sha256(image).hexdigest(), "binary offset:", offset)
    print("NOT TESTED: actual on-device boot, visibility, overlay ordering, touch or RF")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_wallpaper_binary.py <firmware.bin> <patched-UITask.cpp>")
    check(sys.argv[1], sys.argv[2])
