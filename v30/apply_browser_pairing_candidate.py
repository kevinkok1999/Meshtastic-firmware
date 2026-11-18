#!/usr/bin/env python3
"""Opt-in development guard for the inherited plain WebSocket listener.

Not called by apply_v30.py: a real confidential browser transport and verified
physical approval event are mandatory before the candidate can be activated.
"""
from pathlib import Path
import sys


def once(source: str, old: str, new: str) -> str:
    if source.count(old) != 1:
        raise ValueError("browser pairing candidate anchor missing/ambiguous")
    return source.replace(old, new, 1)


def patch(source: str) -> str:
    marker = "// V30_BROWSER_PAIRING_GUARD_ONLY"
    if marker in source:
        if source.count(marker) != 1:
            raise ValueError("browser pairing candidate marker ambiguous")
        return source
    source = once(source, '#include "WebSocketCompanionServer.h"',
        '#include "WebSocketCompanionServer.h"\n#include "V30BrowserSession.h"')
    old = '''          char* key_start = buf + i + 18;
          while (*key_start == ' ' || *key_start == '\\t') key_start++;
          char* key_end = key_start;
          while (*key_end && *key_end != '\\r' && *key_end != '\\n') key_end++;
          size_t key_len = key_end - key_start;
          if (key_len == 0 || key_len > 128) break;'''
    new = '''          // V30_BROWSER_PAIRING_GUARD_ONLY: raw WiFiClient is not TLS.
          // Do not upgrade any browser route (including /mirror and /files),
          // even if Host/Origin are plausible. The separate native root route
          // stays available only to a strict non-browser RFC6455 handshake.
          const auto ip = cl->localIP();
          char host[32], origin[40];
          if (_port == 80) snprintf(host, sizeof(host), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
          else snprintf(host, sizeof(host), "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], _port);
          snprintf(origin, sizeof(origin), "http://%s", host);
          v30_browser::Slice parsed_key{};
          const auto route = v30_browser::validateHandshake(buf, c->handshake_len, host, origin, &parsed_key);
          if (route != v30_browser::Route::Native) {
            const char* denied = "HTTP/1.1 403 Forbidden\\r\\nContent-Length: 0\\r\\nConnection: close\\r\\nCache-Control: no-store\\r\\n\\r\\n";
            (void)writeAllBytes(*cl, reinterpret_cast<const uint8_t*>(denied), strlen(denied), TCP_WRITE_TIMEOUT_MS);
            c->client.stop(); c->in_use = false; return false;
          }
          const char* key_start = parsed_key.p;
          const size_t key_len = parsed_key.n;'''
    source = once(source, old, new)
    # Keep Chats first and avoid endlessly reopening a connection which is
    # intentionally disabled on an unverified plain transport.
    source = once(source, "WS.onclose=function(){stat('reconnecting...','err');setTimeout(conn,1500)};WS.onerror=function(){stat('error','err')};",
        "WS.onclose=function(){stat('Secure browser link unavailable','err');E('tlist').textContent='Beveiligde browserverbinding nog niet beschikbaar. Gebruik het apparaat.'};WS.onerror=function(){stat('Secure browser link unavailable','err')};")
    source = once(source, "page = g_web_mirror.terminalOn() ? WS_HTML_TERMINAL_PAGE : WS_HTTP_INFO_PAGE;", "page = WS_HTML_TERMINAL_PAGE;")
    return source


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: apply_browser_pairing_candidate.py <pinned V30 checkout>')
    target=Path(sys.argv[1])/'src/helpers/esp32/WebSocketCompanionServer.cpp'
    target.write_text(patch(target.read_text()))
