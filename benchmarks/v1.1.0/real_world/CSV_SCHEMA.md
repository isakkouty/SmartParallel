# Real-World Benchmark CSV Schema

Schema version: `4`.

All four integrations use identical schemas. Existing columns are preserved and
new fields are appended.

## Raw repetitions

File: `v1.1.0_real_world_<integration>_raw.csv`

Core fields include workload identity, mode, requested and confirmed backend,
frontier, repetition state, execution time, throughput, memory, task count,
concurrency, scheduler/cache counters, checksums, correctness, workers, and seed.

CPU fields:

| Field | Meaning |
|---|---|
| `cpu_utilization_percent` | Batched process CPU equivalent cores divided by logical processors |
| `process_cpu_equivalent_cores` | Batched process CPU time divided by batched execution wall time |
| `process_cpu_seconds` | Process CPU seconds observed for the individual timed execution interval |
| `cpu_metric_available` | `1` only when the batch duration and physical-range checks permit interpretation |
| `cpu_metric_scope` | `single_repetition`, `timed_repetition_batch`, or `unavailable` |

The cold row may use a single-repetition CPU measurement only when it accumulates
enough observed process-time quanta. Every timed warm row receives the same
aggregate timed-batch CPU value so summary averaging does not reintroduce
per-repetition timer quantization. Short or physically inconsistent batches are
marked unavailable.

## Summary

File: `v1.1.0_real_world_<integration>_summary.csv`

One row per preset/mode, including:

- repetitions, warmups, cold time,
- median, mean, minimum, maximum, standard deviation, p95, and p99,
- throughput, speedup, absolute regret, percentage regret,
- memory, task, concurrency, scheduler, cache, checksum, and validity fields,
- `mean_process_cpu_equivalent_cores`,
- `timed_batch_cpu_seconds`,
- `timed_batch_wall_ms`,
- `cpu_metric_available`.

Regret is calculated against the fastest correct mode for the same integration,
workload, and preset.

## Trace

File: `v1.1.0_real_world_<integration>_trace.csv`

The representative post-warm diagnostic execution records lineage, callsite,
depth, phase, requested and actual backend, confirmation, profile/cache state,
budgets, leases, chunks, helpers, and causal completion timing.

Causal helper fields:

| Field | Meaning |
|---|---|
| `helper_in_flight_work_drain_ms` | Time after unclaimed work is exhausted while claimed callbacks remain |
| `helper_actual_blocking_wait_ms` | Time actually blocked waiting for dependency completion |
| `helper_completion_signal_to_wake_ms` | Notification-to-wake time only when an actual wait occurred |
| `helper_completion_epilogue_ms` | Time after useful completion until backend return |

Trace export is bounded to 1,024 records per preset/mode.

## Environment

File: `v1.1.0_real_world_<integration>_environment.csv`

Important schema-4 metadata includes:

- `benchmark_schema_version=4`
- `backend_calibration_enabled=1`
- `backend_calibration_timed_phase=frozen_after_warmup`
- `backend_calibration_warmup_samples=<configured warmups>`
- `profile_revalidation_timed_phase=frozen_after_warmup`
- `profile_revalidation_production_default=enabled`
- `cpu_metric_semantics=process_cpu_equivalent_cores_quantum_gated_v4`
- `cpu_metric_observed_timer_quantum_seconds=<measured>`
- `cpu_metric_minimum_timer_quanta=8`
- `cpu_metric_minimum_batch_wall_ms=10`
- trace semantics, worker limit, seed, compiler, OS, CPU, dependency versions,
  and integration-specific deterministic dataset metadata.

## Aggregated files

`compare_real_world_results.ps1` produces, using invariant numeric culture:

- `v1.1.0_real_world_comparison.csv`
- `v1.1.0_real_world_auto_analysis.csv`
- `v1.1.0_real_world_analysis.md`

The comparator fails if the Markdown analysis is not created or is empty.
