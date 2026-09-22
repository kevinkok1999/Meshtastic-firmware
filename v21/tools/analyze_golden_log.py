#!/usr/bin/env python3
"""Classify a V21 Golden Wi-Fi serial log without changing any firmware."""

from __future__ import annotations
import re
import sys
from pathlib import Path

REASONS = {
    8: "ASSOC_LEAVE / station left association (often software-initiated)",
    15: "4WAY_HANDSHAKE_TIMEOUT / WPA handshake timeout",
    200: "BEACON_TIMEOUT",
    201: "NO_AP_FOUND",
    202: "AUTH_FAIL",
    203: "ASSOC_FAIL",
    204: "HANDSHAKE_TIMEOUT",
    205: "CONNECTION_FAIL",
}

def main() -> int:
    if len(sys.argv) != 2:
        print("usage: analyze_golden_log.py <serial-log.txt>")
        return 2

    text = Path(sys.argv[1]).read_text(errors="replace")
    connected = "ASSOCIATED_WAIT_DHCP" in text or "association succeeded" in text
    got_ip = "SUCCESS ip=" in text or "phase=GOT_IP" in text

    reasons = [int(x) for x in re.findall(r"(?:reason=|disconnect_reason=)(\d+)", text)]
    nonzero = [r for r in reasons if r != 0]
    last = nonzero[-1] if nonzero else (reasons[-1] if reasons else None)

    stack = re.search(r"\[GOLDEN\] stack=([^\r\n]+)", text)
    arduino = re.search(r"\[GOLDEN\] arduino=([^\r\n]+)", text)
    idf = re.search(r"\[GOLDEN\] idf=([^\r\n]+)", text)

    print("V21 Golden Wi-Fi log analysis")
    print(f"stack: {stack.group(1).strip() if stack else 'unknown'}")
    print(f"arduino: {arduino.group(1).strip() if arduino else 'unknown'}")
    print(f"idf: {idf.group(1).strip() if idf else 'unknown'}")
    print(f"STA association observed: {'YES' if connected else 'NO'}")
    print(f"GOT_IP observed: {'YES' if got_ip else 'NO'}")

    if last is not None:
        print(f"last disconnect reason: {last} — {REASONS.get(last, 'see ESP-IDF Wi-Fi reason documentation')}")
    else:
        print("last disconnect reason: none captured")

    if got_ip:
        print("classification: PASS — association and DHCP both completed")
    elif connected:
        print("classification: DHCP/NETIF branch — association completed but no IP was observed")
    else:
        print("classification: ASSOCIATION branch — failure occurred before a confirmed STA_CONNECTED")

    return 0

if __name__ == "__main__":
    raise SystemExit(main())
