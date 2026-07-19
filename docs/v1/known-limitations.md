# Known limitations

## Tiny workloads

Cold adaptive calls may be slower than direct sequential execution because useful work is smaller than profiling and scheduler overhead. Cached sequential confirmation improves repeated calls but cannot remove the first-call cost.

## Main CPU backend concentration

The recorded automatic decisions heavily favor oneTBB for meaningful parallel work. StaticThread automatic candidates are disabled by default, and the current benchmark oracle compares only forced sequential and forced oneTBB.

## Global state

Configuration, profile cache, experience, and “last decision” diagnostics are global. Configuration mutation during concurrent execution is not a supported usage pattern.

## Callable identity

Profile caching is based partly on callable type and workload bucket. Different runtime captures with the same closure type may have different costs, so periodic revalidation and blending are important but cannot eliminate every misclassification.

## Platform evidence

The strongest recorded validation is from Windows/MSVC. GCC, Clang, installation exports, package-manager ports, and continuous integration remain release-hardening work.

## API scope

v1 provides index-range `parallel_for`; reductions, scans, transforms, sorting, pipelines, cancellation, and GPU-device callbacks are not yet public algorithms.

## Performance guarantees

SmartParallel does not guarantee that adaptive execution will beat direct sequential or a forced backend for every call. The goal is evidence-driven selection with transparent diagnostics and measurable regret.
