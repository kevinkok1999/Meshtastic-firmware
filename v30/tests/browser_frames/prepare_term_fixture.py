from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from apply_browser_framing_v30 import patch_browser_framing
old = Path(__file__).with_name("term_fixture.inc").read_text()
new = patch_browser_framing(old)
assert new != old
assert patch_browser_framing(new) == new
try:
    patch_browser_framing(old + old)
except ValueError:
    pass
else:
    raise AssertionError("duplicate source accepted")
try:
    patch_browser_framing("unknown version")
except ValueError:
    pass
else:
    raise AssertionError("unknown source accepted")
Path(sys.argv[1]).write_text(new)
