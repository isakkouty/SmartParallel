# V1 Phase 2 — Predictive Decision Engine

Phase 2 introduces a candidate-plan cost model while preserving the validated
Beta decision path by default.

## Candidate plans

The initial model evaluates only execution plans that the current executor can
actually represent:

- sequential execution;
- persistent ThreadPool with dynamic chunks;
- StaticThread with static chunks;
- oneTBB with dynamic chunks.

Worker-count and chunk-size exploration are intentionally deferred to Phase 3.
All parallel candidates currently use the same logical-thread count used by the
existing decision path.

## Cost breakdown

Each candidate exposes:

- estimated useful work;
- predicted execution time;
- scheduling overhead;
- common framework/profiling overhead;
- memory-pressure penalty;
- imbalance penalty;
- predicted total time;
- predicted speedup and efficiency;
- confidence.

The first model is deliberately explicit and conservative. Its constants are
starting assumptions, not universal hardware truths. Benchmark calibration is
required before predictive control becomes the default.

## Shadow mode

`Config::enable_predictive_shadow` is enabled by default. In shadow mode the
model records candidates and a recommendation in `DecisionReport`, but the
existing analytical or historical plan remains authoritative.

`Config::enable_predictive_decisions` is disabled by default. Enabling it lets
the predictive recommendation control execution only when its confidence meets
`minimum_predictive_confidence`.

## Why shadow mode matters

A predictive model should not replace validated behavior merely because it
produces plausible formulas. Shadow mode lets the benchmark suite compare:

- predicted winner versus measured winner;
- predicted time versus measured time;
- legacy plan versus predictive recommendation;
- confidence versus prediction error.

The next Phase 2 increment should export these comparisons to benchmark CSVs
and calibrate the model from evidence rather than tuning isolated thresholds.
