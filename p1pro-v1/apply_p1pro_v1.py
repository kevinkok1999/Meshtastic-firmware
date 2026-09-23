#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys

PINNED_MESHCORE_COMMIT = "e94125987ed87497e706a0b54d1e80c709343980"

def fail(message: str) -> None:
    raise SystemExit("P1 Pro V1 patch failed: " + message)

def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)

def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_p1pro_v1.py <pinned MeshCore checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    variant = root / "variants/sensecap_solar/platformio.ini"
    repeater = root / "examples/simple_repeater/MyMesh.cpp"
    common = root / "src/helpers/CommonCLI.h"

    for p in (variant, repeater, common):
        if not p.exists():
            fail("missing expected MeshCore file: " + str(p))

    # Mark only the SenseCAP Solar repeater build. No other MeshCore target is changed.
    v = variant.read_text()
    anchor = """[env:SenseCap_Solar_repeater]
extends = SenseCap_Solar
build_flags =
  ${SenseCap_Solar.build_flags}
"""
    replacement = """[env:SenseCap_Solar_repeater]
extends = SenseCap_Solar
build_flags =
  ${SenseCap_Solar.build_flags}
  -D MESH_OFFGRIDNL_P1PRO_V1=1
  -D MESH_OFFGRIDNL_P1PRO_FLOOD_MAX=48
  -D MESH_OFFGRIDNL_P1PRO_UNSCOPED_MAX=6
  -D MESH_OFFGRIDNL_P1PRO_ADVERT_MAX=8
"""
    v = replace_once(v, anchor, replacement, "SenseCAP Solar V1 build flags")
    variant.write_text(v)

    s = repeater.read_text()

    # Fresh-install defaults. We deliberately do not enlarge StaticPoolPacketManager(32):
    # 48 hops is a path ceiling, not 48 simultaneously queued packets.
    defaults = """  _prefs.flood_max = 64;
  _prefs.flood_max_unscoped = 64;
  _prefs.flood_max_advert = 8;
  _prefs.interference_threshold = 0; // disabled
  _prefs.cad_enabled = 0;            // hardware CAD before TX (off by default; 'set cad on')
"""
    v1_defaults = """#if defined(MESH_OFFGRIDNL_P1PRO_V1)
  // MeshOffGridNL P1 Pro V1: long-path capable but intentionally scoped.
  // 48 is a ceiling; normal known-route traffic should remain as short as possible.
  _prefs.flood_max = MESH_OFFGRIDNL_P1PRO_FLOOD_MAX;
  _prefs.flood_max_unscoped = MESH_OFFGRIDNL_P1PRO_UNSCOPED_MAX;
  _prefs.flood_max_advert = MESH_OFFGRIDNL_P1PRO_ADVERT_MAX;
  _prefs.path_hash_mode = 0;          // one-byte path hashes allow >32-hop paths
  _prefs.loop_detect = LOOP_DETECT_MODERATE;
  _prefs.interference_threshold = 0; // physical-site calibration required before enabling
  _prefs.cad_enabled = 0;            // keep upstream-safe default for first hardware validation
#else
  _prefs.flood_max = 64;
  _prefs.flood_max_unscoped = 64;
  _prefs.flood_max_advert = 8;
  _prefs.interference_threshold = 0; // disabled
  _prefs.cad_enabled = 0;            // hardware CAD before TX (off by default; 'set cad on')
#endif
"""
    s = replace_once(s, defaults, v1_defaults, "P1 V1 fresh defaults")

    # Persisted prefs can come from a previous MeshCore image. Cap them after load so
    # an old 64-hop/global profile cannot silently defeat the V1 congestion contract.
    load_anchor = """  _cli.loadPrefs(_fs);
  acl.load(_fs, self_id);
"""
    load_replacement = """  _cli.loadPrefs(_fs);
#if defined(MESH_OFFGRIDNL_P1PRO_V1)
  if (_prefs.flood_max == 0 || _prefs.flood_max > MESH_OFFGRIDNL_P1PRO_FLOOD_MAX)
    _prefs.flood_max = MESH_OFFGRIDNL_P1PRO_FLOOD_MAX;
  if (_prefs.flood_max_unscoped == 0 || _prefs.flood_max_unscoped > MESH_OFFGRIDNL_P1PRO_UNSCOPED_MAX)
    _prefs.flood_max_unscoped = MESH_OFFGRIDNL_P1PRO_UNSCOPED_MAX;
  if (_prefs.flood_max_advert == 0 || _prefs.flood_max_advert > MESH_OFFGRIDNL_P1PRO_ADVERT_MAX)
    _prefs.flood_max_advert = MESH_OFFGRIDNL_P1PRO_ADVERT_MAX;
  _prefs.path_hash_mode = 0;
  // Do not rely on loop_detect for one-byte paths: collisions are possible.
  // Hop ceilings + duplicate suppression remain the primary safety barriers.
#endif
  acl.load(_fs, self_id);
"""
    s = replace_once(s, load_anchor, load_replacement, "P1 V1 persisted-pref caps")
    repeater.write_text(s)

    # Fast drift checks before a runner spends time compiling.
    variant_text = variant.read_text()
    repeater_text = repeater.read_text()
    common_text = common.read_text()

    required_variant = (
        "MESH_OFFGRIDNL_P1PRO_V1=1",
        "MESH_OFFGRIDNL_P1PRO_FLOOD_MAX=48",
        "MESH_OFFGRIDNL_P1PRO_UNSCOPED_MAX=6",
        "MESH_OFFGRIDNL_P1PRO_ADVERT_MAX=8",
        "-D NRF52_POWER_MANAGEMENT",
        "-D USE_SX1262",
        "-D LORA_TX_POWER=22",
    )
    for marker in required_variant:
        if marker not in variant_text:
            fail("variant missing " + marker)

    required_repeater = (
        "StaticPoolPacketManager(32)",
        "isFloodHopLimitExceeded",
        "_prefs.path_hash_mode = 0;",
        "_prefs.flood_max = MESH_OFFGRIDNL_P1PRO_FLOOD_MAX;",
        "_prefs.flood_max_unscoped = MESH_OFFGRIDNL_P1PRO_UNSCOPED_MAX;",
        "_prefs.flood_max_advert = MESH_OFFGRIDNL_P1PRO_ADVERT_MAX;",
        "discover_limiter(4, 120)",
        "anon_limiter(4, 180)",
    )
    for marker in required_repeater:
        if marker not in repeater_text:
            fail("repeater missing " + marker)

    for marker in (
        "#define LOOP_DETECT_OFF",
        "#define LOOP_DETECT_MODERATE",
    ):
        if marker not in common_text:
            fail("CommonCLI missing " + marker)

    print("P1 Pro V1 phase-1 overlay applied: 48 scoped / 6 unscoped / 8 advert, one-byte path mode, bounded upstream pool")

if __name__ == "__main__":
    main()
