# Known limitations

## Per-root rather than process-wide admission

The nested concurrency budget is enforced independently for each root session. Several external roots may collectively use more participants than one root budget. The global ThreadPool bounds helper capacity, but v1.1 does not promise strict process-wide admission or fairness between sustained competing roots.

## Strict frontier

Once a parallel frontier is selected, descendants use the sequential fast path. This is deterministic and safe but can leave idle capacity on highly skewed or irregular trees. Descendant borrowing is deferred to v1.2.

## Configuration mutation

`global_config()` is process-wide. Concurrent mutation while SmartParallel calls are running is unsupported. Policy signatures invalidate cached plans between calls, but they do not make unsynchronized configuration writes safe.

## Callable identity

Lambdas, function pointers, and many ordinary callsites are distinguished automatically. The same reusable functor type or `std::function` object used for semantically different work may still need an explicit `smart::with_parallel_callsite(key, callback)` wrapper.

## Cancellation

Exception-driven internal cancellation and cleanup are supported. A general public external cancellation-token API is not part of v1.1.

## Long-running retention

Profile cache, frozen root snapshots, and structured trace history are bounded. Enabling very large configured limits intentionally increases retained memory. Trace collection still adds measurable overhead and should be disabled for normal performance measurements.

## Performance guarantees

Automatic execution is not guaranteed to beat a manually selected frontier or `parallel_for_nd` for every workload. Workload drift is periodically revalidated, but the scheduler may remain temporarily suboptimal between revalidation points.
