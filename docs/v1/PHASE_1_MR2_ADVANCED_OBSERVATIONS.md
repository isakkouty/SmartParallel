# V1 Phase 1 — MR-2 Advanced Observations

## Purpose

MR-2 improves where and when SmartParallel samples callback cost. It adds
spatial observations and separates one-time warm-up behavior from steady-state
cost without changing scheduling or backend policy.

## Deterministic spatial sampling

The profiler now uses three sample categories:

- a pilot sample used for batch-size calibration;
- a small local group made of adjacent batches near the middle of the range;
- distributed samples selected through deterministic jittered strata.

The deterministic jitter avoids always hitting the same periodic positions while
keeping benchmark runs reproducible.

## Regional observations

Measured batches are grouped into early, middle, and late regions. The profile
reports each regional median and a regional cost ratio, allowing later decision
logic to distinguish a uniform callback from a workload whose cost changes across
the input domain.

The profiler also reports local and distributed medians and their ratio. This is
a locality-sensitivity proxy only; it is not a cache-miss or bandwidth
measurement.

## Warm-up observations

The first pilot batch is retained separately. Later non-pilot samples provide a
steady-state median.

The profile now reports:

- first-batch cost;
- steady-state cost;
- warm-up ratio;
- estimated one-time setup cost;
- whether warm-up was detected.

Warm-up is reported only when the measurement is reliable, enough later samples
exist, and the configured ratio threshold is exceeded. The profiler does not
infer the cause of the warm-up.

## Diagnostics

The decision report now retains the function profile used during the decision.
The report printer can therefore show robust cost statistics, warm-up data, local
and distributed medians, and regional variation.

These observations remain diagnostic during Phase 1. They do not select an
engine, strategy, worker count, or chunk size.

## Validation

`tests/v1/phase1_advanced_observations.cpp` verifies that:

- local and distributed samples are both collected;
- early, middle, and late regions are observed;
- an intentionally increasing workload produces a larger late-region cost;
- a one-time initialization produces a warm-up ratio and setup-cost estimate;
- deterministic sampling continues to expose the expected regional relationship.

Timing assertions use relationships rather than exact durations.
