# V30 browser pairing: isolated development candidate

The pinned WadaMesh listener is plain HTTP/WS, and the browser's terminal, mirror
and file routes previously reached the upgrade path without a common browser
session authorization. This candidate adds a strict bounded HTTP upgrade parser
and a socket-bound pairing lifecycle **without claiming that plain WS is secure**.

`V30BrowserSession.h` provides fixed-memory 30-second pending requests and
15-minute approved sessions. Both socket slot and non-reusable generation bind
access. A random six-digit display code is not a bearer token; a verified local
hardware approval must match both generation and code. Link loss, timeout,
disconnect or explicit revocation erase authorization. Native WS is separately
classified only on `/` with exact Host and no Origin; browser `/term` requires
the configured exact Origin, valid Host, version, upgrade and canonical key.
Unknown targets, queries, duplicate critical headers and forwarding ambiguity
are rejected. Use the returned parsed key, never scan arbitrary header values.

`apply_browser_pairing_candidate.py` is **opt-in DEV only**, not invoked by
`apply_v30.py`. It installs a guard in the pinned actual listener before the
upgrade response: all browser upgrades are denied on raw WiFiClient, no
credentials or pairing codes travel in a query string, the default page remains
Chats, and rejection does not trigger endless reconnect requests. The strict
native root lane is not a browser-auth solution for the companion protocol.
This guard is not a usable production browser chat path and must not replace the
existing V29/V30 production lane. It is a fail-closed integration checkpoint.

Before activating a real browser pair:

1. Implement and verify a confidential listener (real TLS with browser-trusted
   certificate or explicitly isolated authenticated private AP with a reviewed
   threat model); derive expected origin/host from that listener, never requests.
2. Connect pairing generation to a socket-owned lifecycle under the existing
   recursive client mutex. Generate the display code from CSPRNG. Show the same
   code in browser's pending status and the on-device modal, with explicit
   approve/deny via verified hardware input, never remote touch/companion events.
3. Suppress all application TX/RX before approval; flush queues on revoke and
   socket recycling, and never transfer a session to a replacement socket.
4. Ensure authorized `/term` is chat-first; authorize mirror/files separately
   if required. Test browser reconnect/expiry, CSWSH/DNS rebinding, two devices,
   native-client compatibility and radio/UI responsiveness on physical T-Deck.

Local test: `c++ -std=c++11 -Wall -Wextra -Werror -pedantic -I v30/overlay/src/helpers/esp32 v30/tests/browser_auth/test_session.cpp -o /tmp/v30-browser-test && /tmp/v30-browser-test`.
Pinned integration guard test: `python v30/tests/browser_auth/test_candidate.py <pinned V30 checkout>`.
