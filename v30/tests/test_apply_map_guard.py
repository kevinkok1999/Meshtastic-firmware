import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE=Path(__file__).resolve().parent
V30=HERE.parent if (HERE.parent/'overlay').is_dir() else HERE
SCRIPT=V30/'apply_map_guard_v30.py'
HEADERS=V30/'overlay/src/ui-touch' if (V30/'overlay').is_dir() else HERE
FIXTURE=HERE/'test_guard_fixture.inc'
class GuardPatchTests(unittest.TestCase):
    def test_native_mount_guard_is_active_only_on_v30(self):
        # Compile the exact upstream body after applying the same patch used
        # by the firmware reconstruction; assert actual FS-call side effects.
        if SCRIPT.exists():
            spec=importlib.util.spec_from_file_location('map_guard',SCRIPT)
            mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
        else: mod=None
        source='#include "UITask.h"\nvoid startup(){\n'+FIXTURE.read_text()+'}\n'
        if mod:
            source=mod.patch_ui(source)
            self.assertEqual(mod.patch_ui(source),source)
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp)
            (tmp/'UITask.h').write_text('''#include <cstddef>
#include <cassert>
#define WIRE_DBG(...)
struct FS { bool begin(bool f,const char*,int,const char*) { ++calls; formatting+=f; return true; } size_t totalBytes(){return 0x4c0000;} void format(){++formatting;} void end(){} int calls=0,formatting=0;};
static FS s_tiles_fs; static bool s_tiles_fs_ready=false;
''')
            (tmp/'NanoMapFlash.h').write_text('''#include "NanoMapImage.h"
namespace meshtastic::nanomap { TileStorage classifyInternalTiles(){return TileStorage::NanoMap;} }
''')
            (tmp/'main.cpp').write_text(source+'''int main(){startup();
#if defined(MESH_OFFGRIDNL_V30)
assert(s_tiles_fs.calls==0 && s_tiles_fs.formatting==0 && !s_tiles_fs_ready);
#else
assert(s_tiles_fs.calls==1 && s_tiles_fs.formatting==1 && s_tiles_fs_ready);
#endif
}
''')
            for flags in [[],['-DMESH_OFFGRIDNL_V30']]:
                subprocess.run(['g++','-std=c++17','-DESP32','-I',str(HEADERS),*flags,str(tmp/'main.cpp'),'-o',str(tmp/'run')],check=True)
                result=subprocess.run([str(tmp/'run')],cwd=tmp,capture_output=True)
                self.assertEqual(result.returncode,0,result.stderr.decode())

if __name__=='__main__': unittest.main()
