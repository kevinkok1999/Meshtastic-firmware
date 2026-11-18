import importlib.util
from pathlib import Path
import sys

base=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('candidate',base/'apply_browser_pairing_candidate.py')
candidate=importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)
source=(Path(sys.argv[1])/'src/helpers/esp32/WebSocketCompanionServer.cpp').read_text()
patched=candidate.patch(source)
assert candidate.patch(patched)==patched
assert 'route != v30_browser::Route::Native' in patched
assert patched.index('route != v30_browser::Route::Native') < patched.index('HTTP/1.1 101 Switching Protocols')
assert 'c->client.stop(); c->in_use = false; return false;' in patched
assert 'const char* key_start = parsed_key.p;' in patched
assert "curTab='chats'" in patched
assert 'page = WS_HTML_TERMINAL_PAGE;' in patched
assert 'Beveiligde browserverbinding nog niet beschikbaar.' in patched
assert "WS.onclose=function(){stat('reconnecting...'" not in patched
try:
    candidate.patch(source.replace('#include "WebSocketCompanionServer.h"','// missing'))
except ValueError:
    pass
else:
    raise AssertionError('unrecognized source was accepted')
print('browser candidate: pinned integration guard, exact anchors, idempotence, chat-first passed')
