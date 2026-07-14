# SmartParallel V1 — Phase 1 Sensor Layer Report

## Status

Phase 1 is complete.

The purpose of this phase was to improve what SmartParallel knows before it
makes an execution decision. Decision thresholds, engine scoring, worker-count
selection, chunk sizing, and public APIs were deliberately not redesigned.

## Architecture outcome

SmartParallel now separates observations from policy.

```text
Workload Builder
    -> exact structural facts
Workload Analyzer
    -> derived hardware-relative observations
Function Profiler
    -> sampled cost, variability, spatial, and warm-up observations
Decision Context / Report
    -> one inspectable input for future decision models
Decision Engine
    -> existing policy, unchanged during Phase 1
```

The sensor layer records both provenance and confidence so later decision code
can distinguish exact facts from derived, estimated, sampled, or unavailable
information.

## Correctness work

- Profiling operates on isolated copies when safe.
- Unsupported copy/invocation cases make profiling explicitly unavailable.
- Pair-iteration multiplication uses saturating arithmetic.
- Represented-byte calculations use saturating arithmetic.
- Static partition bounds avoid multiplication overflow.

## Structural observations

The workload layer now records:

- logical iteration count;
- dimensionality and extents;
- represented input bytes;
- unique input elements;
- pair-workload reuse factors;
- storage kind;
- contiguity and random-access capability when provable;
- element stride when knowable;
- iterations per logical thread and physical core;
- L1, L2, and L3 residency ratios;
- elements per cache line and cache lines per element.

These values describe represented storage and iteration structure. They do not
claim to measure actual bytes touched, cache misses, or memory bandwidth.

## Function observations

The profiler now provides:

- process-local timer calibration;
- measurement-floor and signal-quality reporting;
- adaptive batch sizing;
- callback, sample, and time budgets;
- early stopping with explicit stop reasons;
- mean, median, trimmed mean, standard deviation, coefficient of variation,
  p95, tail ratio, and maximum;
- deterministic jittered stratified sampling;
- local and distributed sampling groups;
- early, middle, and late regional medians;
- regional and distributed/local cost ratios;
- first-batch and steady-state cost separation;
- warm-up detection and estimated one-time setup cost;
- confidence and provenance metadata.

The trimmed mean remains the robust primary cost estimate used to derive total
work and parallel worthiness for compatibility with the current decision rules.

## Canonical observation integration

The performance-model builder now consumes the structural observation model as
its canonical source for:

- logical iterations;
- represented input bytes;
- cache pressure ratios when hardware cache observations are available.

Compatibility fields remain for the current Beta decision rules, but new model
code no longer needs to independently recalculate the same sensor values.

## Diagnostics

Decision diagnostics are grouped into:

- Structure;
- Hardware-relative scale;
- Function profile;
- Decision.

The report includes observation source, confidence, sampling budget usage,
measurement reliability, stop reason, spatial behavior, warm-up behavior, and
whether the performance model consumed sensor cache ratios directly.

## Validation suite

Phase 1 includes five focused tests:

1. `smartparallel_phase1_profiling_semantics`
2. `smartparallel_phase1_sensor_layer`
3. `smartparallel_phase1_advanced_observations`
4. `smartparallel_phase1_sensor_validation`
5. `smartparallel_phase1_profiler_budget`

The tests validate relationships rather than exact timings. Examples include:

- heavy callbacks cost more than cheap callbacks;
- spatially increasing work has a larger regional ratio than uniform work;
- large represented datasets have larger cache-residency ratios than small
  datasets;
- pair reuse factors are correct;
- profiler callback, sample, and time budgets remain bounded;
- the performance model consumes canonical structural observations.

## Compatibility fields retained

The following fields remain temporarily because the current decision engine
uses them:

- `working_set_bytes`;
- `is_small`;
- `has_many_iterations`;
- `objects_are_large`;
- `is_memory_heavy`;
- `instability_ratio`;
- `stable`;
- `parallel_worthiness`.

They are compatibility outputs, not the long-term source of truth. They should
only be removed after Phase 2 replaces the heuristic decision model with an
explicit predictive model.

## Explicitly rejected or deferred

Phase 1 does not claim to measure:

- actual memory traffic;
- cache misses;
- arithmetic intensity;
- branchiness;
- vectorization potential;
- exact memory randomness.

Also deferred:

- hardware performance counters;
- forced cache flushing;
- parallel scaling probes;
- NUMA-aware execution;
- adaptive workers and chunk sizes;
- decision-rule tuning;
- backend redesign;
- machine-learning models.

## Known limitations

- Wall-clock profiles can still be influenced by operating-system scheduling,
  power management, and unrelated machine activity.
- Storage topology describes the container representation, not captured or
  indirectly referenced memory.
- Warm-up detection identifies a timing pattern but cannot infer its cause.
- Local/distributed ratios are locality proxies, not proof of a memory-bound
  workload.
- Some callback types cannot be safely copied for isolated profiling; their
  profile is explicitly unavailable.

## Phase 2 input

Phase 2 can now consume a reliable and inspectable set of observations to build
candidate-plan cost estimates. The next decision model should use the sensor
layer through explicit formulas and confidence handling instead of attaching
independent heuristics to every new metric.
