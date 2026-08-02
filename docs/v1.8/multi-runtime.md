# Multi-Runtime coordination

Several Runtime instances may share one governor while retaining isolated Runtime state.

Shared:

- process admission budget;
- queue and fairness state;
- contention diagnostics.

Isolated:

- Runtime options;
- profile database;
- adaptive route evidence;
- numerical defaults;
- deterministic state;
- operation fingerprints.

The v1.8 validation runs two, four, and eight concurrent Runtime instances and checks that each root operation acquires one lease, total active participation remains bounded, and all permits return.
