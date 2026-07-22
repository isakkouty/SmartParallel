# Real-World Benchmark CSV Schema

Schema version: `2`.

All four integrations use identical schemas. The existing nested-validation CSV
files retain their original columns. Version 2 preserves existing real-world
columns and appends the new CPU and causal helper-timing fields.

## Raw repetitions

File: `v1.1.0_real_world_<integration>_raw.csv`

| Field | Meaning |
|---|---|
| `integration`, `workload`, `preset`, `parameters` | Workload identity and deterministic parameters |
| `mode` | Requested comparison mode |
| `requested_backend` | Backend constraint requested by the mode |
| `actual_backend` | Backend confirmed by execution, or `sequential` |
| `selected_strategy`, `selected_frontier` | Strategy and frontier classification |
| `repetition` | Zero for cold; 1..N for timed warm samples |
| `state` | `cold` or `warm` |
| `execution_ms` | Timed useful execution only |
| `throughput_per_second` | Work units divided by execution time |
| `cpu_utilization_percent` | Process CPU time divided by wall time and logical processors |
| `process_cpu_equivalent_cores` | Process CPU time divided by wall time; 1.0 means one fully occupied core |
| `peak_memory_bytes` | Process peak working-set estimate |
| `task_count` | Logical workload task/update count |
| `max_concurrency` | Maximum useful-work concurrency or traced runtime concurrency |
| `scheduler_decisions` | Decisions in the representative diagnostic execution |
| `cache_hits`, `stable_plan_reuse` | Cache/snapshot reuse counters |
| `checksum`, `expected_checksum`, `correct` | Correctness evidence |
| `exception_status`, `cancellation_status` | Execution outcome fields |
| `workers`, `seed` | Reproduction settings |

Untimed warmups are not mixed into raw timed samples. Their count is recorded in
summary and environment metadata.

## Summary

File: `v1.1.0_real_world_<integration>_summary.csv`

One row per preset/mode, including identity and strategy fields plus:

- `repetitions`, `warmups`, `cold_ms`
- `median_ms`, `mean_ms`, `minimum_ms`, `maximum_ms`
- `standard_deviation_ms`, `p95_ms`, `p99_ms`
- `throughput_per_second`
- `speedup_over_sequential`
- `absolute_regret_ms`, `percentage_regret`
- `mean_cpu_utilization_percent`
- `mean_process_cpu_equivalent_cores`
- memory, scheduler, cache, concurrency, checksum, and validity fields

Regret is calculated against the fastest correct mode for the same integration,
workload, and preset.

## Trace

File: `v1.1.0_real_world_<integration>_trace.csv`

The trace records the representative post-warm diagnostic execution. Existing
fields remain, including root/loop lineage, callsite identity, depth, phase,
requested/actual backend, backend confirmation, profile/cache state, budgets,
leases, chunks, and helpers.

Version 2 appends these causal helper fields:

| Field | Meaning |
|---|---|
| `helper_in_flight_work_drain_ms` | Time from the caller exhausting unclaimed work until all claimed callbacks complete |
| `helper_actual_blocking_wait_ms` | Time the caller actually spent blocked waiting for dependency completion |
| `helper_completion_epilogue_ms` | Time after all callbacks completed until the backend completion path returned |
| `helper_completion_signal_to_wake_ms` | Notification-to-wake interval only when an actual wait occurred; otherwise zero |

`helper_wait_count` should be used with the wake metric. A zero wait count means
there was no condition-variable blocking interval to interpret.

Trace export remains sampled and bounded to 1,024 records per preset/mode.

## Environment

File: `v1.1.0_real_world_<integration>_environment.csv`

Key/value metadata includes:

- SmartParallel version and benchmark source identity
- schema version and trace limit
- compiler, build type, operating system, CPU model
- logical processor count and selected worker limit
- timestamp, seed, warmup and repetition counts
- oneTBB/OpenCV/LZ4 availability and versions
- deterministic dataset description
- frontier descendant direct-mode status
- session-local plan memo status
- analytical cold-start status
- backend-calibration status
- CPU metric semantics

## Aggregated files

`compare_real_world_results.ps1` produces:

- `v1.1.0_real_world_comparison.csv`
- `v1.1.0_real_world_auto_analysis.csv`
- `v1.1.0_real_world_analysis.md`
