# Frequently asked questions

> **Current documentation:** SmartParallel v1.1.0.

## Does SmartParallel replace oneTBB?

No. oneTBB is one supported backend. SmartParallel adds workload selection, stable runtime learning, and cross-backend nested coordination around the loop callsite.

## Why not always run in parallel?

Dispatch and synchronization can exceed useful work for small callbacks or ranges. Memory bandwidth, imbalance, and nesting also change the best strategy.

## Are iterations executed during profiling and then repeated?

No. Sampling and cold-learning paths preserve exactly-once execution by excluding sampled indices from later work or learning from the real completed execution.

## Is experience persisted between application launches?

Not by default. The runtime database is in memory for the process lifetime. Explicit load/save APIs and persistence configuration exist, but they are opt-in.

## Can I force a backend?

Yes, through `global_config().execution_engine`. Configuration is process-global and should be set before concurrent execution.

## What happens in nested loops?

Nested calls inherit a root session and budget. SmartParallel selects a bounded frontier and may execute descendants sequentially rather than creating another full team.

## Is automatic execution always fastest?

No. It is designed to approach a strong tested plan without manual tuning. The benchmark report includes cases where a manual or backend-specific strategy wins.

## Can callbacks throw?

Yes. The runtime cooperatively cancels remaining work where possible, cleans up backend participants and leases, and propagates the original exception.

## Why are traces slower?

Structured tracing records synchronized per-loop state and should be treated as a diagnostic mode, not a normal benchmark mode.
