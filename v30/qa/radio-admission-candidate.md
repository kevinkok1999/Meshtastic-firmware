# V30 dormant all-packet admission candidate

Not RF activation or regulatory evidence. Standard V30 applier unchanged. Opt-in DEV workflow compiles pinned Dispatcher hooks twice with MESH_OFFGRIDNL_V30_EXPERIMENTAL=0 and compiles new bridge/identity module; no instantiation or radio feed.

- One owner, fixed 16-slot pool, address + monotonic allocation generation. Distinct allocate, enqueue accepted, dispatch, detach, release. Stale free/completion cannot claim reused slot.
- Shared bridge consumes live dispatch token and estimate in ms; microsecond ledger and full quiet window after cold construction. Reject unqualified active profile. Enum is not physical RF proof.
- Charge max reservation, callback-reported elapsed, monotonic admission-to-completion. Unknown/mismatched completion, clock rollback, overrun latch fault. Unresolved reserve never discarded. Failed start still charges reserve. Not a recipient receipt.
- V30 cookie admits only against actual bridge reservation. Exact-token queue rejection retires only original cookie; no copy ownership.

## Remaining gates
Pinned StaticPoolPacketManager queueOutbound returns void and frees on add failure. MyMesh verifies queue presence and compares packet hashes on reuse. Wire lifecycle transitions to actual allocation/release, enqueue accepted/rejected, Dispatcher dequeue, direct manager detach/requeue, RX retransmit and free paths. No pointer-only binding.
Install one shared bridge before startSendRaw and all three physical completion paths, including native and retransmits; prove no bypass. Qualify actual settings and ERP/emissions externally before enabling. Measure estimates, verify monotonic clock and quiet-window reboot handling. No ledger restore or MyMesh override currently exists. Real HIL, lawful rolling-hour budget and verified recipient ACK remain blocked.
