# Automatic loop optimization

> **Runtime documentation:** SmartParallel v1.1 behavior, retained by the current [v1.3 portability release](../v1.3/README.md).

SmartParallel v1.0 introduced runtime strategy selection for a single loop; v1.1 retains that system and applies it within a nested execution context.

## What the scheduler decides

A candidate plan describes:

- sequential or parallel execution;
- backend selection;
- worker/job count;
- static or dynamic chunking;
- chunk size;
- confidence and predicted runtime.

The runtime combines analytical estimates, measured callback profiles, machine calibration, workload fingerprints, residual corrections, and bounded historical evidence. Risk and confidence controls limit how much immature history can override conservative predictions.

## Tiny and cold workloads

Small workloads are especially sensitive to profiling and dispatch overhead. SmartParallel uses exactly-once sampling or analytical/pilot cold starts, cached sequential fast paths, and measured tiny-work bypass rules. These mechanisms reduce overhead but do not guarantee that automatic execution always matches the fastest manual strategy for sub-millisecond loops.

## Stable plans

Repeated compatible callsites can reuse a stable plan. Keys include callsite identity, workload/context buckets, root budget/backend context, and a scheduler-policy generation. Production revalidation can refresh stale plans when workloads drift.
