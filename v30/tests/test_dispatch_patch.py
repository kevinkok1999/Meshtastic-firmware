#!/usr/bin/env python3
"""Host regression for the opt-in V30 Dispatcher completion hook candidate."""
from __future__ import annotations

import importlib.util
import pathlib
import tempfile
import urllib.request

REPO = pathlib.Path(__file__).resolve().parents[2]
PATCH = REPO / "v30/extra_scripts/v30_dispatch_patch.py"
APPLIER = REPO / "v30/apply_v30.py"

HEADER = """#pragma once
class Packet;
using DispatcherAction = unsigned;
class Dispatcher {
protected:
  virtual DispatcherAction onRecvPacket(Packet* pkt) = 0;
};
"""

SOURCE = """#include "Dispatcher.h"
void Dispatcher::loop() {
  if (outbound) {
    if (_radio->isSendComplete()) {
      long t = _ms->getMillis() - outbound_start;
      logTx(outbound, 2 + outbound->getPathByteLen() + outbound->payload_len);
      _radio->onSendFinished();
      releasePacket(outbound);
      outbound = NULL;
    } else if (millisHasNowPassed(outbound_expiry)) {
      _radio->onSendFinished();
      logTxFail(outbound, 2 + outbound->getPathByteLen() + outbound->payload_len);
      releasePacket(outbound);
      outbound = NULL;
    }
  }
}
void Dispatcher::checkSend() {
  if (outbound) {
    if (len + outbound->payload_len > MAX_TRANS_UNIT) {
      _mgr->free(outbound);
      outbound = NULL;
    } else {
      uint32_t max_airtime = _radio->getEstAirtimeFor(len)*3/2;
      outbound_start = _ms->getMillis();
    bool success = _radio->startSendRaw(outbound_raw, len);
    if (!success) {
        logTxFail(outbound, outbound->getRawLength());
      releasePacket(outbound);
      outbound = NULL;
    }
    }
  }
}
void Dispatcher::sendPacket(Packet* packet, uint8_t priority, uint32_t delay_millis) {
  if (!Packet::isValidPathLen(packet->path_len) || packet->payload_len > MAX_PACKET_PAYLOAD) {
    _mgr->free(packet);
  } else {
    if (packet->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
    }
  }
}
"""

def load():
    spec = importlib.util.spec_from_file_location("v30_dispatch_patch", PATCH)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def main() -> None:
    module = load()
    header, source = module.patch_sources(HEADER, SOURCE)
    assert header.count("onV30TxComplete") == 1
    assert source.count("onV30TxComplete") == 3
    assert header.count("onV30TxAdmission") == 1
    assert header.count("onV30TxDropped") == 1
    assert source.count("onV30TxAdmission") == 1
    assert source.count("onV30TxDropped") == 3
    assert "onV30TxComplete(outbound, true, static_cast<uint32_t>(t));" in source
    assert "onV30TxComplete(outbound, false, static_cast<uint32_t>(_ms->getMillis() - outbound_start));" in source
    assert "onV30TxComplete(outbound, false, 0);" in source
    try:
        module.patch_sources(HEADER, SOURCE.replace("bool success = _radio->startSendRaw", "bool started = _radio->startSendRaw"))
    except module.PatchError:
        pass
    else:
        raise AssertionError("unsupported source shape must fail closed")
    applier = APPLIER.read_text(encoding="utf-8")
    assert "v30_dispatch_patch.py" not in applier
    assert "custom_v30_dispatch_patch" not in applier
    # Test the actual pinned dependency, not just the shape fixture.
    base = f"https://raw.githubusercontent.com/ALLFATHER-BV/meshcomod/{module.PIN}/src/"
    with tempfile.TemporaryDirectory() as td:
        root = pathlib.Path(td)
        for name, expected in (("Dispatcher.h", module.HEADER_BLOB), ("Dispatcher.cpp", module.SOURCE_BLOB)):
            with urllib.request.urlopen(base + name, timeout=30) as response:
                raw = response.read()
            assert module.git_blob_id(raw) == expected
            (root / name).write_bytes(raw)
        hp, sp = module.patch_dependency(root)
        assert hp.read_text().count("onV30TxComplete") == 1
        assert sp.read_text().count("onV30TxComplete") == 3
        assert hp.read_text().count("onV30TxAdmission") == 1
        assert hp.read_text().count("onV30TxDropped") == 1
        assert sp.read_text().count("onV30TxAdmission") == 1
        assert sp.read_text().count("onV30TxDropped") == 3
        # Pre-scripts run for each PlatformIO compile, including rebuilds.
        patched_pair = (hp.read_bytes(), sp.read_bytes())
        assert module.patch_dependency(root) == (hp, sp)
        assert (hp.read_bytes(), sp.read_bytes()) == patched_pair

        with urllib.request.urlopen(base + "Dispatcher.h", timeout=30) as response:
            original_header = response.read()
        hp.write_bytes(original_header)
        try:
            module.patch_dependency(root)
        except module.PatchError:
            pass
        else:
            raise AssertionError("partially patched core accepted")

        hp.write_bytes(patched_pair[0])
        sp.write_bytes(patched_pair[1] + b"\n// hand-edited\n")
        try:
            module.patch_dependency(root)
        except module.PatchError:
            pass
        else:
            raise AssertionError("modified patched core accepted")

        sp.write_bytes(patched_pair[1])
        duplicate = root / "duplicate"
        duplicate.mkdir()
        (duplicate / "Dispatcher.h").write_bytes(patched_pair[0])
        (duplicate / "Dispatcher.cpp").write_bytes(patched_pair[1])
        try:
            module.patch_dependency(root)
        except module.PatchError:
            pass
        else:
            raise AssertionError("ambiguous pinned core accepted")
    print("V30 Dispatcher hook candidate tests passed")

if __name__ == "__main__":
    main()
