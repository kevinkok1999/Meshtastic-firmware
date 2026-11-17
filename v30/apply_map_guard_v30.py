#!/usr/bin/env python3
"""Protect the immutable NanoMap image from WadaMesh's legacy tile auto-format.

Apply only to the reconstructed V30 checkout, not any historical V29 source.
The pinned native map remains unactivated pending background/UI/signed gates.
"""
from pathlib import Path
import sys

INCLUDE='#include "UITask.h"\n'
NEW_INCLUDE=INCLUDE+'#if defined(ESP32) && defined(MESH_OFFGRIDNL_V30)\n#include "NanoMapFlash.h"\n#endif\n'
BEGIN='''  s_tiles_fs_ready = s_tiles_fs.begin(true /*formatOnFail*/, "/tiles_lfs",
                                       10 /*maxOpenFiles*/, "tiles");
#if defined(ESP32)
'''
NEW_BEGIN='''#if defined(ESP32) && defined(MESH_OFFGRIDNL_V30)
  // V30: never mount a raw NanoMap image as LittleFS; no implicit format.
  s_tiles_fs_ready = meshtastic::nanomap::mountLegacyTilesWithoutFormat(
      s_tiles_fs, meshtastic::nanomap::classifyInternalTiles());
#else
'''+BEGIN.removesuffix('#if defined(ESP32)\n')+'''#endif
#if defined(ESP32)
#if !defined(MESH_OFFGRIDNL_V30)
'''
END='  // Pick the active tile-cache backend now'
NEW_END='#endif // V30 does not erase absent/corrupt/raw tile storage\n\n'+END

def once(text,old,new):
    if text.count(old)!=1: raise ValueError(f'map guard expected one anchor, found {text.count(old)}: {old[:64]}')
    return text.replace(old,new,1)

def patch_ui(text):
    if all(x in text for x in (NEW_INCLUDE,NEW_BEGIN,NEW_END)): return text
    if any(x in text for x in (NEW_INCLUDE,NEW_BEGIN,NEW_END)): raise ValueError('incomplete previous map guard patch')
    start=text.index(BEGIN)
    end=text.index(END,start)
    if text[start:end].count('s_tiles_fs.format();')!=2:
        raise ValueError('unexpected pinned auto-format path')
    return once(once(once(text,INCLUDE,NEW_INCLUDE),BEGIN,NEW_BEGIN),END,NEW_END)

def patch_pio(text):
    start=text.index('[env:LilyGo_TDeck_companion_radio_touch]')
    end=text.index('[env:LilyGo_TDeck_Pro_companion_radio_touch]',start)
    section=text[start:end]
    include='  +<ui-touch/miniz/*.c>\n'
    if include not in section:
        section=once(section,'  +<ui-touch/*.c>\n','  +<ui-touch/*.c>\n'+include)
    return text[:start]+section+text[end:]

def apply(root):
    root=Path(root)
    ui=root/'src/ui-touch/UITask.cpp'; pio=root/'platformio.ini'
    new_ui=patch_ui(ui.read_text(encoding='utf-8'))
    new_pio=patch_pio(pio.read_text(encoding='utf-8'))
    ui.write_text(new_ui,encoding='utf-8');pio.write_text(new_pio,encoding='utf-8')

if __name__=='__main__':
    if len(sys.argv)!=2: raise SystemExit('usage: apply_map_guard_v30.py <V30 reconstructed checkout>')
    apply(sys.argv[1])
