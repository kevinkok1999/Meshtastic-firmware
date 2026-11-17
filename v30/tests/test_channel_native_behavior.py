#!/usr/bin/env python3
from pathlib import Path
import subprocess, sys, tempfile, unittest

ROOT=Path(__file__).resolve().parents[1]
class NativeBehaviorTest(unittest.TestCase):
 def test_generated_native_callback_behavior(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)
   ui=root/"src/ui-touch/UITask.cpp"
   ui.parent.mkdir(parents=True)
   (root/"platformio.ini").write_text("[env:LilyGo_TDeck_companion_radio_touch]\n  -D MESH_OFFGRIDNL_V30=1\n[env:LilyGo_TDeck_Pro_companion_radio_touch]\n")
   (root/"src/MyMesh.cpp").write_text("// fixture\n")
   (root/"src/MyMesh.h").write_bytes((ROOT/"tests/channel_native_stub.h").read_bytes())
   ui.write_bytes((ROOT/"tests/channel_native_fixture.cpp").read_bytes())
   subprocess.run([sys.executable,str(ROOT/"apply_channel_v30.py"),str(root)],check=True)
   exe=root/"native"
   subprocess.run(["c++","-std=c++17",str(ui),"-o",str(exe)],check=True)
   subprocess.run([str(exe)],check=True)

if __name__=="__main__": unittest.main()
