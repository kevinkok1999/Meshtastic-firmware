"""Candidate V30-only patch for MeshCore Dispatcher completion callbacks.

This module is intentionally not wired by apply_v30.py. It is a deterministic,
fail-closed candidate for a later build once the pinned dependency is compiled.
"""

from __future__ import annotations

from pathlib import Path

HEADER_ANCHOR = "  virtual DispatcherAction onRecvPacket(Packet* pkt) = 0;\n"
HEADER_INSERT = (
    HEADER_ANCHOR
    + "#if defined(MESH_OFFGRIDNL_V30) && MESH_OFFGRIDNL_V30\n"
    + "  // V30 hook: called only after the radio reports completion/failure.\n"
    + "  virtual void onV30TxComplete(Packet* packet, bool success, uint32_t airtimeMs) { }\n"
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
    return header, source


def locate_dependency(libdeps: Path) -> tuple[Path, Path]:
    candidates = sorted(libdeps.rglob("Dispatcher.h"))
    for header in candidates:
        source = header.with_name("Dispatcher.cpp")
        if source.exists():
            return header, source
    raise PatchError("pinned MeshCore Dispatcher.h/.cpp not found")


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
