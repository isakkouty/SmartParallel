# Frequently asked questions

## Why not always use oneTBB?

Because scheduler setup and task dispatch can cost more than tiny callbacks. Sequential execution and lower-overhead alternatives remain necessary.

## Why does SmartParallel profile the callback?

Iteration count alone does not reveal callback cost. Sampling provides direct evidence while preserving exactly-once semantics.

## Does profiling execute iterations twice?

No. Sampled indices are recorded and excluded from the remaining execution gaps.

## Why are some benchmark decisions wrong?

The benchmark oracle is known only after measuring forced alternatives. Tiny timing differences are difficult to predict, and adaptive overhead itself contributes to regret. These misses are reported openly.

## Does SmartParallel replace oneTBB scheduling?

No. It decides when and how to use oneTBB; oneTBB retains responsibility for task scheduling and work stealing.

## Can callbacks have side effects?

Yes, provided each iteration's effect is independent or properly synchronized. The callback may execute on different worker threads and in an unspecified order.

## Is the experience database required?

No. In-memory experience is enabled by default, but persistence across runs is disabled by default.

## Can I force a backend?

Yes, through `global_config().execution_engine`. Forced execution is especially useful for controlled benchmarks and diagnosis.
