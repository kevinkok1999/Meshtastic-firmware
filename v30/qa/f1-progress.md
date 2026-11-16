# F1 transport progress

Status: implementation checkpoint, not a release and not production verified.

- Baseline: V30 branch commit `10f95ff8028b3566c1faaa504a1ae43cc3e571d3` with specialist preflight preserved.
- Pinned inputs: WadaMesh `6fe6b9f06332708b6ed0cc78958017d0dcc9e35d`; MeshCore `core-v1.17.4` resolved to `edd7ed47f42acf6c64c9fcac551050fdbd531a09`.
- Observed CI red run `43976156194`: host fixture passed; it stopped at the intentional missing-`apply_v30.py` assertion. No firmware build or hardware evidence was claimed.
- This checkpoint adds a host-compiled bounded direct adapter and an overlay patch. Admission, physical TX completion, and recipient delivery remain separate. The adapter does not synthesize `onV30TxResult`; dispatcher integration must call it at actual completion/failure.
- The full V11→V29 reconstruction, PlatformIO T-Deck build, queue/drop behavior of the pinned Dispatcher, and RF/HIL evidence are still unverified in this environment. Keep F1/F5 gates blocked until those checks run.
