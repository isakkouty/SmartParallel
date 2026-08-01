# SmartParallel v1.7 execution lineage

v1.7 retains the existing bounded nested-execution lineage and trace machinery while adding Runtime and semantic-operation fingerprints.

## Available observability

Applications can inspect:

- the Runtime fingerprint;
- the last semantic-operation fingerprint;
- Runtime telemetry counters;
- existing scheduler and nested-execution diagnostics;
- stable cross-process run manifests for the supplied replay pilot.

Operation fingerprints include the selected route, scheduler, worker budgets, provider, numerical contract, workload identity, profile identities, warm-start state, and Deterministic state.

## Deferred public scope API

A proposed public named `ExecutionScope` API and general JSON execution-tree export were intentionally deferred. The mandatory release first stabilized Runtime ownership, persistent exact profiles, approval, deterministic replay, tooling, and regression gates.

Deferring that optional surface avoids freezing a public lineage schema before its naming, lifetime, privacy, and aggregation contracts are ready. The existing internal lineage and validated nested coordination remain unchanged.
