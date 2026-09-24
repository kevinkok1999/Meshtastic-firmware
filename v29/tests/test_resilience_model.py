#!/usr/bin/env python3
from __future__ import annotations
import pathlib, re, sys

def die(msg: str) -> None:
    raise SystemExit("V29 resilience failure: " + msg)

def numeric(text: str, name: str) -> int:
    m = re.search(rf"{name}\s*=\s*(\d+)", text)
    if not m:
        die("missing constant " + name)
    return int(m.group(1))

def main() -> None:
    if len(sys.argv) != 2:
        die("usage: test_resilience_model.py <patched-wadamesh>")
    root = pathlib.Path(sys.argv[1]).resolve()
    h = (root / "src/helpers/esp32/V29EmergencyFabric.h").read_text()
    cpp = (root / "src/helpers/esp32/V29EmergencyFabric.cpp").read_text()
    portal = (root / "src/helpers/esp32/V29EmergencyPortal.cpp").read_text()

    if numeric(h, "PROTOCOL_VERSION") != 2: die("protocol must be v2")
    if numeric(h, "HEADER_LEN") != 82: die("header must be 82 bytes")
    if numeric(h, "DEFAULT_TTL_HOURS") != 192: die("TTL must be 192 hours")
    offsets=[numeric(h,n) for n in ("AGE_MINUTES_OFFSET","MSG_ID_OFFSET","ORIGIN_OFFSET","RECIPIENT_HINT_OFFSET","NONCE_OFFSET")]
    if offsets != [12,14,30,62,70]: die("wire offsets changed: " + repr(offsets))

    for marker in (
        "out[AGE_MINUTES_OFFSET] = 0",
        "out[AGE_MINUTES_OFFSET + 1] = 0",
        "aad[AGE_MINUTES_OFFSET] = 0",
        "aad[AGE_MINUTES_OFFSET + 1] = 0",
        "r.ageMinutesBase = wireAgeMinutes(wire)",
        "setWireAgeMinutes(r.wire",
    ):
        if marker not in cpp: die("cumulative-age marker missing " + marker)

    ttl = numeric(h, "DEFAULT_TTL_HOURS") * 60
    age=0
    for leg in (37,421,1440,2880,5000):
        age += leg
        if age >= ttl: die("pre-expiry vector too large")
        lo,hi=age & 255,(age>>8)&255
        if (lo | (hi<<8)) != age: die("age encoding reset")

    dedup="if (seenOrRemember(data + MSG_ID_OFFSET)) return true;"
    route="if (recipientIsSelf(data))"
    if dedup not in cpp or cpp.index(dedup) > cpp.index(route):
        die("relay dedup is after routing")

    for marker in ("MAX_CRITICAL_PER_ORIGIN","trimCriticalOrigin","removeRecord((uint16_t)oldest)"):
        if marker not in h + "\n" + cpp: die("critical queue guard missing " + marker)

    for marker in ("method='post'","const bool isPost","Allow: POST","form-action 'self'","actionAllowed"):
        if marker not in portal: die("portal safety marker missing " + marker)
    for forbidden in ("href='/safe","href='/help","href='/moving","href='/meeting"):
        if forbidden in portal: die("unsafe GET state change remains " + forbidden)

    for marker in ("/v29q0.bin","/v29q1.bin","snapshotValid","generation"):
        if marker not in cpp: die("double-snapshot recovery missing " + marker)

    print("V29 resilience model OK: cumulative 192h TTL, relay dedup, bounded critical queue, POST-only portal, double-snapshot recovery")

if __name__ == "__main__":
    main()
