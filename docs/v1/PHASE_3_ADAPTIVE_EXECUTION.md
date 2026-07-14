# SmartParallel V1 Phase 3 — Adaptive Execution

Phase 3 expands predictive candidates from one fixed full-machine plan per backend to multiple execution configurations.

## Implemented

- Worker-count candidates: powers of two plus the machine maximum.
- Per-plan dynamic chunk-size estimation from measured callback cost.
- ThreadPool dynamic chunk acquisition with bounded active workers.
- oneTBB task-arena concurrency limits and blocked-range grain sizes.
- StaticThread worker-count candidates.
- Experience keys and persistence include chunk size (`V3` format, with V2 compatibility).
- Validation compares exact backend, worker count, and chunk size.

## Safety

Predictive execution remains opt-in. Shadow mode continues to expose all adaptive candidates without changing the selected analytical plan.

## Current limits

- Worker counts are selected before execution; plans do not switch backend mid-loop.
- Chunk size is fixed for one execution and may be refined by later experience.
- ThreadPool worker count limits active worker tasks, while the persistent pool itself retains the machine thread count.
