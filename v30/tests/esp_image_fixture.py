"""Independently constructed unsigned ESP32-S3 image for verifier tests.

Not executable firmware. Constants follow the ESP32-S3 image format and the
ESP-IDF application descriptor layout; no production validator is imported.
"""
import functools
import hashlib
import operator
import struct


def esp_image(kind: str, *, append_digest: bool = True) -> bytes:
    if kind == 'bootloader':
        segments = [(0x40378000, b'\x11\x22\x33\x44')]
    elif kind in ('app0', 'app1'):
        desc = bytes.fromhex('3254cdab') + bytes(252)
        segments = [(0x3c000020, desc), (0x40378000, b'\x11\x22\x33\x44')]
    else:
        raise ValueError(kind)
    header = struct.pack('<BBBBI', 0xe9, len(segments), 2, 0x40, 0x40378000)
    # Disabled write protect pin, chip ID 9, unrestricted chip revision, hash flag.
    extended = struct.pack('<BBBBHBHH4BB', 0xee, 0, 0, 0, 9, 0, 0, 0xffff,
                           0, 0, 0, 0, int(append_digest))
    payload = b''.join(struct.pack('<II', addr, len(data)) + data for addr, data in segments)
    data = header + extended + payload
    checksum = functools.reduce(operator.xor, b''.join(x[1] for x in segments), 0xef)
    data += bytes((15 - len(data)) % 16) + bytes([checksum])
    return data + hashlib.sha256(data).digest() if append_digest else data
