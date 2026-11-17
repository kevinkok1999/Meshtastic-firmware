"""Opt-in exact-pinned lifecycle observation patch for V30 DEV builds.

It changes no default native queue outcome. The observer is null unless
explicitly attached at idle startup; no observer installation is in V30.
"""
from pathlib import Path
import hashlib

PIN = "edd7ed47f42acf6c64c9fcac551050fdbd531a09"
HEADER_BLOB = "59715b4e018eeee7a3ecfb926d9ee4fd7e54d494"
SOURCE_BLOB = "b8926df0cc0045a065123d04296f9b534895f632"
PATCHED_HEADER_BLOB = "af25e5578ea39c79f966be91ee423aa8e82ae39f"
PATCHED_SOURCE_BLOB = "20806cb8184615df772bd55eba753de7ed8ae92f"
GUARD = "#if defined(MESH_OFFGRIDNL_V30_POOL_OBSERVER) && MESH_OFFGRIDNL_V30_POOL_OBSERVER\n"

class PatchError(RuntimeError):
    pass

def git_blob_id(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\x00" + data).hexdigest()

def once(text, old, new, label):
    if text.count(old) != 1:
        raise PatchError(f"{label}: expected exactly one anchor")
    return text.replace(old, new, 1)

def guarded(body):
    return GUARD + body + "#endif\n"

def patch_sources(header: str, source: str) -> tuple[str, str]:
    h = "#include <Dispatcher.h>\n"
    header = once(header, h, h + guarded("""
enum class V30PoolEvent : uint8_t {
  Allocated, OutboundQueued, OutboundRejected, Dispatching, Detached,
  InboundQueued, InboundDequeued, InboundRejected, Released, ReleaseFailed
};
class V30PoolObserver {
public:
  virtual ~V30PoolObserver() = default;
  virtual void onV30PoolEvent(V30PoolEvent, const mesh::Packet*) = 0;
};
"""), "observer contract")
    h = "  PacketQueue unused, send_queue, rx_queue;\n"
    header = once(header, h, h + guarded("""  V30PoolObserver* _v30Observer = nullptr;
  int _v30PoolSize = 0;
  void notifyV30Pool(V30PoolEvent event, const mesh::Packet* packet);
"""), "observer storage")
    h = "  StaticPoolPacketManager(int pool_size);\n"
    header = once(header, h, h + guarded("""  // Observer owns no packets and must outlive this manager.
  bool setV30Observer(V30PoolObserver* observer);
"""), "observer attachment")

    a = "  // load up our unusued Packet pool\n"
    source = once(source, a, guarded("  _v30PoolSize = pool_size;\n") + a, "pool size")
    a = """mesh::Packet* StaticPoolPacketManager::allocNew() {
  return unused.removeByIdx(0);  // just get first one (returns NULL if empty)
}
"""
    b = guarded("""bool StaticPoolPacketManager::setV30Observer(V30PoolObserver* observer) {
  if (!observer || _v30Observer || _v30PoolSize <= 0 ||
      unused.count() != _v30PoolSize || send_queue.count() != 0 || rx_queue.count() != 0)
    return false;
  _v30Observer = observer;
  return true;
}

void StaticPoolPacketManager::notifyV30Pool(V30PoolEvent event, const mesh::Packet* packet) {
  if (_v30Observer) _v30Observer->onV30PoolEvent(event, packet);
}
""") + """
mesh::Packet* StaticPoolPacketManager::allocNew() {
  mesh::Packet* packet = unused.removeByIdx(0);
""" + guarded("  if (packet) notifyV30Pool(V30PoolEvent::Allocated, packet);\n") + """  return packet;
}
"""
    source = once(source, a, b, "allocation")
    a = """void StaticPoolPacketManager::free(mesh::Packet* packet) {
  unused.add(packet, 0, 0);
}
"""
    b = "void StaticPoolPacketManager::free(mesh::Packet* packet) {\n" + GUARD + """  const bool returned = unused.add(packet, 0, 0);
  notifyV30Pool(returned ? V30PoolEvent::Released : V30PoolEvent::ReleaseFailed, packet);
#else
  unused.add(packet, 0, 0);
#endif
}
"""
    source = once(source, a, b, "release")
    a = """    MESH_DEBUG_PRINTLN("queueOutbound: send queue full, dropping packet");
    free(packet);
  }
"""
    b = """    MESH_DEBUG_PRINTLN("queueOutbound: send queue full, dropping packet");
""" + guarded("    notifyV30Pool(V30PoolEvent::OutboundRejected, packet);\n") + """    free(packet);
  } else {
""" + guarded("    notifyV30Pool(V30PoolEvent::OutboundQueued, packet);\n") + "  }\n"
    source = once(source, a, b, "outbound outcome")
    a = "  return send_queue.get(now);\n"
    b = "  mesh::Packet* packet = send_queue.get(now);\n" + guarded("  if (packet) notifyV30Pool(V30PoolEvent::Dispatching, packet);\n") + "  return packet;\n"
    source = once(source, a, b, "outbound dequeue")
    a = "  return send_queue.removeByIdx(i);\n"
    b = "  mesh::Packet* packet = send_queue.removeByIdx(i);\n" + guarded("  if (packet) notifyV30Pool(V30PoolEvent::Detached, packet);\n") + "  return packet;\n"
    source = once(source, a, b, "outbound detach")
    a = """    MESH_DEBUG_PRINTLN("queueInbound: rx queue full, dropping packet");
    free(packet);
  }
"""
    b = """    MESH_DEBUG_PRINTLN("queueInbound: rx queue full, dropping packet");
""" + guarded("    notifyV30Pool(V30PoolEvent::InboundRejected, packet);\n") + """    free(packet);
  } else {
""" + guarded("    notifyV30Pool(V30PoolEvent::InboundQueued, packet);\n") + "  }\n"
    source = once(source, a, b, "inbound outcome")
    a = "  return rx_queue.get(now);\n"
    b = "  mesh::Packet* packet = rx_queue.get(now);\n" + guarded("  if (packet) notifyV30Pool(V30PoolEvent::InboundDequeued, packet);\n") + "  return packet;\n"
    source = once(source, a, b, "inbound dequeue")
    return header, source

def patch_dependency(libdeps):
    allowed = {(HEADER_BLOB, SOURCE_BLOB), (PATCHED_HEADER_BLOB, PATCHED_SOURCE_BLOB)}
    found = []
    for h in sorted(Path(libdeps).rglob("StaticPoolPacketManager.h")):
        s = h.with_name("StaticPoolPacketManager.cpp")
        if s.is_file() and (git_blob_id(h.read_bytes()), git_blob_id(s.read_bytes())) in allowed:
            found.append((h, s))
    if len(found) != 1:
        raise PatchError(f"expected exactly one original or exact patched pool pair, got {len(found)}")
    hp, sp = found[0]
    h, s = hp.read_bytes(), sp.read_bytes()
    pair = git_blob_id(h), git_blob_id(s)
    if pair == (PATCHED_HEADER_BLOB, PATCHED_SOURCE_BLOB):
        return hp, sp
    if pair != (HEADER_BLOB, SOURCE_BLOB):
        raise PatchError("pool source changed after discovery")
    h, s = (part.encode("utf-8") for part in patch_sources(h.decode("utf-8"), s.decode("utf-8")))
    if (git_blob_id(h), git_blob_id(s)) != (PATCHED_HEADER_BLOB, PATCHED_SOURCE_BLOB):
        raise PatchError("pool patch differs from exact expected bytes")
    hp.write_bytes(h)
    sp.write_bytes(s)
    return hp, sp

try:
    Import("env")
except NameError:
    pass
else:
    if env.GetProjectOption("custom_v30_pool_patch", default="") == "1":
        hp, sp = patch_dependency(env.subst("$PROJECT_LIBDEPS_DIR"))
        print("V30_POOL_PATCH_APPLIED", hp, sp)
