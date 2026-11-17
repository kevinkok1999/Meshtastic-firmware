"""Independent fixture for the pinned WadaMesh T-Deck partition CSV."""
import hashlib
import struct


ROWS = (
    ("nvs", 1, 2, 0x9000, 0x5000),
    ("otadata", 1, 0, 0xE000, 0x2000),
    ("app0", 0, 0x10, 0x10000, 0x3E0000),
    ("app1", 0, 0x11, 0x3F0000, 0x3E0000),
    ("tiles", 1, 0x83, 0x7D0000, 0x4C0000),
    ("spiffs", 1, 0x82, 0xC90000, 0x360000),
    ("coredump", 1, 3, 0xFF0000, 0x10000),
)


def partition_image(rows=ROWS):
    raw = b"".join(struct.pack("<HBBII16sI", 0x50AA, kind, subtype, start, size, label.encode(), 0)
                   for label, kind, subtype, start, size in rows)
    raw += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(raw).digest()
    return raw.ljust(0xC00, b"\xff")
