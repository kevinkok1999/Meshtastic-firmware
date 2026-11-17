#!/usr/bin/env python3
"""Compile real exact pinned pool implementation; no radio or crypto exercised."""
import importlib.util
import pathlib
import subprocess
import tempfile
import urllib.request

REPO = pathlib.Path(__file__).resolve().parents[2]
PATCH = REPO / "v30/extra_scripts/v30_pool_patch.py"
OVERLAY = REPO / "v30/overlay/src/helpers/esp32"
TEST = pathlib.Path(__file__).with_name("test_pool_observer.cpp")
spec = importlib.util.spec_from_file_location("v30_pool_patch", PATCH)
assert spec and spec.loader
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

FILES = {
    "Dispatcher.h": "90d0b237b75669a4fb2b878afb9633642adccf25",
    "MeshCore.h": "d8bbff4cc58588c73de0c9278e7050e25b045545",
    "Identity.h": "008f7b5bf63293e2d1ef332cadbf9cc6ec51129e",
    "Utils.h": "7fe2c5314879a1e93d243ed476e01e5331ab9a59",
    "Packet.h": "c19d9e9d8fc9bb3bfab26f3d5808978200083dd9",
    "Packet.cpp": "aad3e2f48ee0b9484d398b78c81940f91e51e0c8",
    "helpers/StaticPoolPacketManager.h": mod.HEADER_BLOB,
    "helpers/StaticPoolPacketManager.cpp": mod.SOURCE_BLOB,
}

def main():
    with tempfile.TemporaryDirectory(prefix="v30-real-pool-") as td:
        root = pathlib.Path(td)
        for name, blob in FILES.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            url = f"https://raw.githubusercontent.com/ALLFATHER-BV/meshcomod/{mod.PIN}/src/{name}"
            with urllib.request.urlopen(url, timeout=30) as response:
                data = response.read()
            assert mod.git_blob_id(data) == blob
            path.write_bytes(data)
        hp, sp = mod.patch_dependency(root)
        original = hp.read_bytes(), sp.read_bytes()
        assert mod.patch_dependency(root) == (hp, sp)
        assert (hp.read_bytes(), sp.read_bytes()) == original
        sp.write_bytes(original[1] + b"\n// modified\n")
        try:
            mod.patch_dependency(root)
        except mod.PatchError:
            pass
        else:
            raise AssertionError("modified pool pair accepted")
        sp.write_bytes(original[1])
        # Declarations only for unused platform/crypto methods. The actual
        # pinned Packet constructor and actual PacketQueue are compiled.
        (root/"Stream.h").write_text("#pragma once\nclass Stream;\n")
        (root/"SHA256.h").write_text("#pragma once\n#include <stddef.h>\nclass SHA256 { public: void update(const void*, size_t); void finalize(void*, size_t); };\n")
        cmd = ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
               "-Wno-unused-parameter", "-Wno-reorder", # immutable upstream header warnings
               "-ffunction-sections", "-fdata-sections", "-I", str(root), "-I", str(OVERLAY),
               str(TEST), str(sp), str(root/"Packet.cpp"), "-Wl,--gc-sections"]
        for opt_in in (False, True):
            exe = root / ("opt-in" if opt_in else "native-default")
            args = cmd[:]
            if opt_in:
                args += ["-DMESH_OFFGRIDNL_V30_POOL_OBSERVER=1",
                         str(OVERLAY/"V30PoolObserverAdapter.cpp"),
                         str(OVERLAY/"V30RadioAdmissionBridge.cpp"),
                         str(OVERLAY/"V30AirtimeGovernor.cpp")]
            subprocess.run(args + ["-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
    print("V30 exact-pinned real pool default and opt-in observer contracts passed")

if __name__ == "__main__":
    main()
