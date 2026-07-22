# Architecture

> **Runtime documentation:** SmartParallel v1.1 behavior, retained by the current [v1.3 portability release](../v1.3/README.md).

## End-to-end flow

```text
smart::parallel_for
  -> resolve callsite and execution context
  -> root session creation or nested-session inheritance
  -> profile/cache lookup and workload estimation
  -> candidate generation and strategy/backend ranking
  -> nested frontier and budget coordination
  -> Sequential | ThreadPool | StaticThread | oneTBB
  -> telemetry, stable-plan reuse, and optional experience recording
```

## Major components

| Component | Responsibility |
|---|---|
| Profiling and workload model | Estimates callback cost, structure, and workload family. |
| Decision model | Produces and ranks sequential/parallel candidate plans. |
| Experience database | Retains bounded runtime observations for the current process and supports optional explicit persistence. |
| Nested execution session | Owns root identity, worker budget, leases, cancellation state, plan snapshots, and traces. |
| Nested coordinator | Resolves backend capabilities, inherited budget, and frontier policy. |
| Backends | Execute through the caller, persistent ThreadPool, fixed StaticThread team, or constrained oneTBB arena. |
| Diagnostics | Exposes profiling decisions and structured nested execution traces. |

## Safety boundaries

- The callback range is half-open and each iteration is executed exactly once on success.
- Participant leases are held for the lifetime of actual backend participants.
- Exceptions trigger cooperative cancellation, backend cleanup, and propagation to the root caller.
- Stable plans and caches are bounded and generation-keyed.
- Nested descendants reuse the active root domain or fall back sequentially when no safe budget remains.

## State model

`global_config()` is process-global. Profiles, experience, calibration state, and traces are shared runtime facilities protected by their implementation-specific synchronization. Configuration itself should be treated as startup state, not a concurrently mutable control plane.
