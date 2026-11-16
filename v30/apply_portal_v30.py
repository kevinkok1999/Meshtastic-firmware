#!/usr/bin/env python3
"""V30-only portal reconstruction. Refuse unknown V29 source bytes/anchors."""
from pathlib import Path
import hashlib
import sys
ROOT = Path(__file__).resolve().parent
PINNED = {
    "V29EmergencyPortal.h": "c4209139dd8be030d0ee970abc3527e18bca2ad0",
    "V29EmergencyPortal.cpp": "1e6f8bbcdaf03c1112cab683affe818c6db6fb11",
}
def blob(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\x00" + data).hexdigest()
def once(text, old, new):
    if text.count(new)==1 and text.count(old)==0:
        return text
    if text.count(old)!=1 or text.count(new)!=0:
        raise ValueError("V30 portal source anchor mismatch")
    return text.replace(old,new,1)
def apply(root):
    root = Path(root)
    writes = []
    for name, expected in PINNED.items():
        path = root / "src/helpers/esp32" / name
        current = path.read_bytes()
        new = (ROOT / "patches/portal" / name).read_bytes()
        if current != new and blob(current) != expected:
            raise ValueError("V30 portal pinned source mismatch: " + name)
        writes.append((path,new))
    ui=root / "src/ui-touch/UITask.cpp"
    old=(ROOT / "patches/portal/callback_v29.inc").read_text()
    new=(ROOT / "patches/portal/callback_v30.inc").read_text()
    writes.append((ui, once(ui.read_text(),old,new).encode()))
    ws=root / "src/helpers/esp32/WebSocketCompanionServer.cpp"
    source=ws.read_text()
    anchor="void WebSocketCompanionServer::serviceMirror(WebMirror& m) {\n  WsClientsLock _lk(_client_mtx);          // held for the whole (quick, non-blocking) pass\n"
    guard=anchor+"""#if defined(MESH_OFFGRIDNL_V30)
  if (v29_emergency_portal.active()) {
    for (int i=0; i<WS_COMPANION_MAX_CLIENTS; ++i) disconnectClient(i);
    m.noteClients(0); m.noteTermClients(0); m.pushPointer(0,0,0);
    return;
  }
#endif
"""
    # The replacement includes the old prefix; idempotency is explicit.
    if guard not in source:
        if source.count(anchor)!=1: raise ValueError("V30 portal WS service anchor mismatch")
        source=source.replace(anchor,guard,1)
    inc='#if defined(MESH_OFFGRIDNL_V30)\n#include "V29EmergencyPortal.h"\n#endif\n'
    if inc not in source: source=inc+source
    writes.append((ws,source.encode()))
    for path,data in writes: path.write_bytes(data)
if __name__=="__main__":
    if len(sys.argv)!=2: raise SystemExit("usage: apply_portal_v30.py <reconstructed V30 checkout>")
    apply(sys.argv[1])
