# Validation guide

## Deterministic tests

- `parallel_for_auto_profile` verifies automatic callback profiling and exactly-once behavior.
- `parallel_for_validation` checks range semantics, cache behavior, backend decisions, and fast-path conditions.
- `parallel_for_hardening` stresses exceptions, boundaries, repetition, and concurrency-sensitive cases.
- Phase 1 tests validate regret metrics, ranking, decision context, model persistence, and hybrid runtime behavior.

## Measurement programs

- `parallel_for_overhead` separates cold and cached scheduling phases.
- `prediction_validation` generates calibration data.
- `holdout_validation` generates independent holdout data.
- `machine_calibration_report` measures machine-specific priors.

## Release gate

A release should satisfy all of the following:

1. Clean configure and Release build.
2. All deterministic tests pass.
3. Every benchmark emits a CSV and completes without crash or hang.
4. Every numerical `correct` or `output_correct` field is true.
5. Decision quality, regret, and overhead are reviewed rather than hidden.
6. Compiler, dependency, operating-system, and hardware information are archived with results.

## Phase 1 workflow

```bat
scriptsalidation
un_v1_phase1.bat
```

This builds the validation targets, regenerates calibration and holdout data, audits dataset readiness, and evaluates the utility ranker.
