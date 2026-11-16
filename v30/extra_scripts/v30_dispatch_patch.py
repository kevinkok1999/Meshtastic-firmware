"""Candidate V30-only patch for MeshCore Dispatcher completion callbacks.

This module is intentionally not wired by apply_v30.py. It is a deterministic,
fail-closed candidate for a later build once the pinned dependency is compiled.
"""

from __future__ import annotations

from pathlib import Path
import hashlib

PIN = "edd7ed47f42acf6c64c9fcac551050fdbd531a09"
HEADER_BLOB = "90d0b237b75669a4fb2b878afb9633642adccf25"
SOURCE_BLOB = "350671ec0889f259947763a2d33074b36d158e30"

def git_blob_id(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\x00" + data).hexdigest()


HEADER_ANCHOR = "  virtual DispatcherAction onRecvPacket(Packet* pkt) = 0;\n"
HEADER_INSERT = (
    HEADER_ANCHOR
    + "#if defined(MESH_OFFGRIDNL_V30) && MESH_OFFGRIDNL_V30\n"
    + "  // V30 hook: called only after the radio reports completion/failure.\n"
    + "  virtual bool onV30TxAdmission(Packet*, uint32_t) { return true; }\n"
    + "  virtual void onV30TxDropped(Packet*, uint8_t) { }\n"
    + "  virtual void onV30TxComplete(Packet*, bool, uint32_t) { }\n"
    + "#endif\n"
)
SUCCESS_ANCHOR = (
    "      logTx(outbound, 2 + outbound->getPathByteLen() + outbound->payload_len);\n"
)
SUCCESS_INSERT = (
    "#if defined(MESH_OFFGRIDNL_V30) && MESH_OFFGRIDNL_V30\n"
    "      onV30TxComplete(outbound, true, static_cast<uint32_t>(t));\n"
    "#endif\n"
    + SUCCESS_ANCHOR
)
TIMEOUT_ANCHOR = (
    "      logTxFail(outbound, 2 + outbound->getPathByteLen() + outbound->payload_len);\n"
)
TIMEOUT_INSERT = (
    "#if defined(MESH_OFFGRIDNL_V30) && MESH_OFFGRIDNL_V30\n"
    "      onV30TxComplete(outbound, false, static_cast<uint32_t>(_ms->getMillis() - outbound_start));\n"
    "#endif\n"
    + TIMEOUT_ANCHOR
)
INVALID_ANCHOR = (
    "      _mgr->free(outbound);\n"
    "      outbound = NULL;\n"
    "    } else {\n"
)
INVALID_INSERT = (
    "#if defined(MESH_OFFGRIDNL_V30) && MESH_OFFGRIDNL_V30\n"
    "      onV30TxDropped(outbound, 1); // invalid wire length\n"
    "#endif\n"
    + INVALID_ANCHOR
)
ADMISSION_ANCHOR = (
    "      uint32_t max_airtime = _radio->getEstAirtimeFor(len)*3/2;\n"
    "      outbound_start = _ms->getMillis();\n"
)
ADMISSION_INSERT = (
    "      uint32_t max_airtime = _radio->getEstAirtimeFor(len)*3/2;\n"
    "#if defined(MESH_OFFGRIDNL_V30) && MESH_OFFGRIDNL_V30\n"
    "      if (!onV30TxAdmission(outbound, max_airtime)) {\n"
    "        onV30TxDropped(outbound, 2); // admission denied\n"
    "        releasePacket(outbound);\n"
    "        outbound = NULL;\n"
    "        outbound_raw_len = 0;\n"
    "        return;\n"
    "      }\n"
    "#endif\n"
    "      outbound_start = _ms->getMillis();\n"
)
START_FAIL_ANCHOR = (
    "        logTxFail(outbound, outbound->getRawLength());\n"
)
START_FAIL_INSERT = (
    "#if defined(MESH_OFFGRIDNL_V30) && MESH_OFFGRIDNL_V30\n"
    "        onV30TxComplete(outbound, false, 0);\n"
    "#endif\n"
    + START_FAIL_ANCHOR
)


class PatchError(RuntimeError):
    pass


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise PatchError(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def patch_sources(header: str, source: str) -> tuple[str, str]:
    if "class Dispatcher" not in header or "virtual DispatcherAction onRecvPacket(Packet* pkt) = 0;" not in header:
        raise PatchError("unsupported Dispatcher.h shape")
    if "bool success = _radio->startSendRaw(outbound_raw, len);" not in source:
        raise PatchError("unsupported Dispatcher.cpp send-start shape")
    header = replace_once(header, HEADER_ANCHOR, HEADER_INSERT, "completion declaration")
    source = replace_once(source, SUCCESS_ANCHOR, SUCCESS_INSERT, "completion success")
    source = replace_once(source, TIMEOUT_ANCHOR, TIMEOUT_INSERT, "completion timeout")
    source = replace_once(source, START_FAIL_ANCHOR, START_FAIL_INSERT, "completion start failure")
    source = replace_once(source, INVALID_ANCHOR, INVALID_INSERT, "invalid wire length drop")
    source = replace_once(source, ADMISSION_ANCHOR, ADMISSION_INSERT, "pre-start admission")
    return header, source


def locate_dependency(libdeps: Path) -> tuple[Path, Path]:
    matching = []
    for header in sorted(libdeps.rglob("Dispatcher.h")):
        source = header.with_name("Dispatcher.cpp")
        if source.is_file() and git_blob_id(header.read_bytes()) == HEADER_BLOB and git_blob_id(source.read_bytes()) == SOURCE_BLOB:
            matching.append((header, source))
    if len(matching) != 1:
        raise PatchError(f"expected exactly one unmodified pinned MeshCore source pair, got {len(matching)}")
    return matching[0]


def patch_dependency(libdeps: str | Path) -> tuple[Path, Path]:
    header_path, source_path = locate_dependency(Path(libdeps))
    header, source = header_path.read_text(encoding="utf-8"), source_path.read_text(encoding="utf-8")
    if "onV30TxComplete" in header or "onV30TxComplete" in source:
        raise PatchError("dependency already contains V30 completion hook")
    patched_header, patched_source = patch_sources(header, source)
    header_path.write_text(patched_header, encoding="utf-8")
    source_path.write_text(patched_source, encoding="utf-8")
    return header_path, source_path


# PlatformIO entry point is deliberately opt-in. apply_v30.py does not add this
# extra script; a future build must explicitly opt into the candidate.
if "Import" in globals():
    Import("env")
    if env.GetProjectOption("custom_v30_dispatch_patch", default="") == "1":
        patch_dependency(env.subst("$PROJECT_LIBDEPS_DIR"))
