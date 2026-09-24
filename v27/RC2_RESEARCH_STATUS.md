# V27 RC2 Work — Research Status

This branch is NOT the Stable implementation source.

Purpose:
- preserve experiments performed after RC1;
- validate ideas before selective extraction into the final V27 implementation waves.

Approved reusable research:
- verified TLS client mechanics;
- four-stage 2.4-GHz association research;
- 128-bit DM/group route-capability experiment;
- incremental subscription setup;
- cached pairwise-secret lookup;
- fixed-size encrypted-envelope mechanics.

Known intentional limitations:
- public EMQX endpoint remains development-only;
- no production relay authentication/ACL/revocation;
- no multi-region failover;
- no global-first Transport Orchestrator;
- no V27 sandboxed unknown-open auto-connect;
- no production protocol-v3 128-bit message ID;
- no durable replay/message journal;
- no recipient delivery receipts;
- no secure OTA/device lifecycle implementation.

Rules:
- never merge this branch wholesale into Stable;
- never modify P1 Pro V8, V26, V19 or older releases from this branch;
- final implementation starts from pinned v27-rc1 and selectively ports reviewed pieces according to v27-master-design.
