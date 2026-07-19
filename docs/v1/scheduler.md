# Scheduler and decision model

## What the scheduler decides

For each call, SmartParallel chooses:

- sequential or parallel execution;
- execution engine;
- scheduling strategy;
- maximum worker count;
- dynamic chunk size when applicable.

## Candidate plans

`ExecutionStrategy` has three values:

- `Sequential` — one caller thread, no parallel scheduler;
- `StaticChunks` — preassigned contiguous ranges;
- `DynamicChunks` — runtime-balanced chunks.

The current automatic path primarily uses sequential and oneTBB dynamic candidates. StaticThread automatic candidates are disabled by default until their calibration is sufficiently stable, although StaticThread remains available explicitly.

## Prediction layers

1. **Profile extrapolation:** estimates total useful callback work from sampled iterations.
2. **Framework cost:** accounts for profiling, analysis, decision, and scheduling overhead.
3. **Hardware-aware scaling:** limits unrealistic scaling under core, cache, SMT, and NUMA pressure.
4. **Memory models:** distinguish cache-resident, streaming, latency-bound, and large-working-set behavior.
5. **Historical calibration:** corrects systematic prediction error when enough stable evidence exists.
6. **Residual learning:** applies bounded actual-versus-analytical correction.
7. **Confidence and risk ranking:** penalizes uncertain candidates and guards learned overrides.

## Why oneTBB remains the main CPU backend

oneTBB already provides mature task decomposition, task arenas, locality-aware execution, and work stealing. SmartParallel adds value above that runtime: deciding *whether* to use it, limiting concurrency, and comparing it with lower-overhead alternatives. Reimplementing a generic work-stealing scheduler is not a v1 goal.

## Tiny workloads

Tiny workloads are intentionally not hidden. Their useful work can be smaller than profiling and dispatch costs, so the adaptive choice can lose even when the underlying backend is efficient. The cached sequential fast path reduces repeat-call overhead after enough consistent observations, but cold calls remain a known limitation.
