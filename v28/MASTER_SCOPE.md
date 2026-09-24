# MeshOffGridNL V28 — Master Scope

V28 starts from the frozen V27 RC1 release commit:

- Base: 4815b06245279b24861732c2cdec59c86e3f11e5
- V27 remains immutable.
- P1 Pro V8 remains immutable.
- V26, V19 and all older releases remain immutable.

All new work belongs under V28:
- production relay / store-and-forward
- functionality-preserving privacy hardening
- opportunistic open-Wi-Fi sandbox
- worldwide connectivity hardening
- delivery receipts and durable idempotency
- production recovery / lifecycle hardening
- V28-only RF Intelligence extensions
- installer/release changes labeled V28

Privacy rule:
Privacy must cooperate with functionality. A failed privacy/global path may reject that unsafe path, but must never disable Wi-Fi association, local UI, RF fallback, or P1/legacy compatibility.

Release rule:
No V28 work may modify or republish existing V27 artifacts. V28 may reuse V27 behavior through inheritance/adapters, but every new feature is V28-gated.


## RF-first transport rule

V28 changes the V27 global-first policy for V28 devices only.

Transport order:
1. RF/LoRa is always the first send route.
2. If usable Wi-Fi + Internet + secure relay are available, the same logical message is also sent over the Internet as route 2.
3. Both transports use the same logical message ID.
4. The receiver deduplicates RF + Internet copies into one visible message.
5. If Wi-Fi/Internet is unavailable, RF behavior remains fully functional.
6. Internet/privacy failure may never cancel a valid RF send.
7. P1 Pro V8, V27, V26, V19 and older firmware remain unchanged.

Worldwide behavior:
- RF provides the immediate local/off-grid route.
- Internet extends the same message worldwide whenever connectivity exists.
- No user-facing RF/Internet selector is required.
