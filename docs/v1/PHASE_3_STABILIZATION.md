# Phase 3 stabilization: worker-aware machine calibration

Phase 3 expanded the candidate space from backend-only choices to complete
execution plans containing backend, worker count, and chunk size. The first
validation run showed that the previous machine calibration was measured only
at maximum parallelism and then extrapolated to every worker count. That was
not sufficiently accurate for the larger search space.

This update adds a bounded, process-local calibration grid. For each available
worker count (2, 4, 8, ... maximum) and backend, SmartParallel measures:

- dispatch/scheduling overhead at two range sizes;
- compute-like scaling using an integer arithmetic probe;
- streaming-like scaling using a contiguous memory transform.

The predictive model uses the nearest measured worker point. It classifies a
workload conservatively as streaming-like only when storage is known to be
contiguous, the represented input is significant relative to L3, and the
measured callback is very cheap. Compute-like classification requires a more
expensive and reasonably stable callback. Everything else uses a blended
calibration.

These classes are calibration categories, not claims about exact memory
traffic or arithmetic intensity.

Predictive control remains opt-in. Calibration is used in shadow validation
until the expanded candidate space reaches acceptable regret.
