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
