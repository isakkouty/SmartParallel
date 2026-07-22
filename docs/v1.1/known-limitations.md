# Known limitations

> **Runtime documentation:** SmartParallel v1.1 behavior, retained by the current [v1.3 portability release](../v1.3/README.md).

## Per-root admission

The concurrency budget is enforced independently for each root session. Unrelated external roots can collectively exceed one root's budget, subject to backend capacity. v1.1 does not provide strict process-wide fairness or admission control.

## Strict frontier

Once a parallel frontier is selected, descendants normally use a sequential fast path. This avoids oversubscription and repeated dispatch, but highly skewed trees can leave available capacity unused.

## Process-global configuration

`global_config()` must not be mutated concurrently with active SmartParallel calls.

## Runtime identity

Reusable functor types or `std::function` objects used for unrelated work may need `with_parallel_callsite` to avoid sharing a profile identity.

## Experience lifetime

The normal release workflow learns within one process. Optional persistence APIs exist, but persistence is disabled by default and does not imply a portable cross-machine model.

## Cancellation API

Exception-driven cooperative cancellation is supported. A general public external cancellation-token interface is not part of v1.1.

## Automatic optimality

Automatic scheduling aims to choose a strong plan without manual backend/frontier tuning. It is not guaranteed to be the fastest valid strategy for every workload. The final suite's difficult cases include gradual particle-count drift, few-large-image imbalance, and relative regret on sub-millisecond loops.

## Measurement scope

The final benchmark machine used a four-worker limit. Results do not establish universal scaling across processor counts, NUMA systems, GPUs, or other compilers and operating systems.
