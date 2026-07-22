# Runtime learning

> **Runtime documentation:** SmartParallel v1.1 behavior, retained by the current [v1.3 portability release](../v1.3/README.md).

## Profiles

SmartParallel profiles callback cost and workload structure using exactly-once sampling or post-execution telemetry. Profiles are bounded, keyed by callsite and execution context, and reused only when the complete key and policy generation match.

## In-process experience

Completed executions can update the runtime experience database with observed time, prediction error, regret, success rate, and confidence. This database is primarily an in-memory facility for the lifetime of one process.

Optional explicit `save_experience` and `load_experience` APIs exist, and persistence-related configuration fields are present, but persistence is disabled by default and is not required for v1.1 operation.

## Stable-plan reuse

A sufficiently supported plan can be reused without repeating the full decision path. Session-local memos further avoid redundant nested resolution within one root execution.

## Revalidation and drift

Production execution periodically revalidates stable profiles. Contradictory observations can invalidate a stable plan. The final benchmark suite freezes backend calibration and profile revalidation after warm-up so steady-state timing does not mix learning cost with measured execution; production defaults remain adaptive.

## What this is not

The release does not claim a persistent cross-application AI model. Runtime learning is bounded statistical adaptation based on measured executions. Persistent learning and broader learned policies remain optional research directions.
