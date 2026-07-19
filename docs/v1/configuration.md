# Configuration reference

`smart::global_config()` returns the process-wide `Config`. Defaults are designed for safe adaptive execution. Change settings before concurrent calls.

## Core and profiling

| Field | Default | Meaning |
|---|---:|---|
| `enable_timing_diagnostics` | `false` | Records detailed phase timing. |
| `enable_experience` | `true` | Records runtime observations in memory. |
| `enable_parallel_for_auto_profiling` | `true` | Samples unknown callbacks before planning. |
| `parallel_for_profile_min_samples` | `8` | Minimum callback samples. |
| `parallel_for_profile_max_samples` | `64` | Maximum callback samples. |
| `parallel_for_profile_min_signal_ms` | `0.01` | Minimum elapsed signal considered reliable. |
| `parallel_for_estimated_overhead_ms` | `1.0` | Conservative scheduler-overhead prior. |
| `parallel_for_profile_regions` | `3` | Number of range regions considered by sampling. |
| `enable_parallel_for_profile_cache` | `true` | Reuses callback profiles. |
| `parallel_for_profile_cache_min_hits` | `1` | Hits required before cache reuse. |
| `parallel_for_profile_cache_blend` | `0.25` | Weight of a new observation during blending. |
| `parallel_for_minimum_predicted_speedup` | `1.10` | Minimum predicted gain required for parallelism. |
| `parallel_for_imbalance_penalty` | `1.10` | Penalty for unstable callback cost. |

## Sequential fast path

| Field | Default | Meaning |
|---|---:|---|
| `enable_parallel_for_cached_sequential_fast_path` | `true` | Bypasses planning for repeatedly confirmed cheap callbacks. |
| `parallel_for_sequential_fast_path_min_observations` | `3` | Independent observations required. |
| `parallel_for_sequential_fast_path_speedup_margin` | `0.85` | Requires a comfortably sub-break-even prediction. |
| `parallel_for_sequential_fast_path_revalidate_interval` | `16` | Fast-path uses before revalidation. |

## Workload and candidate generation

| Field | Default | Meaning |
|---|---:|---|
| `small_workload_iteration_threshold` | `1,000` | Structural small-workload boundary. |
| `cheap_workload_sequential_threshold` | `100,000` | Cheap-callback sequential prior. |
| `many_iterations_threshold` | `1,000,000` | Large iteration-count boundary. |
| `enable_adaptive_execution_candidates` | `true` | Generates multiple worker-count candidates. |
| `enable_chunk_neighborhood_candidates` | `false` | Tests neighboring dynamic chunk sizes. |
| `minimum_adaptive_workers` | `2` | Minimum parallel worker candidate. |
| `target_dynamic_chunk_ms` | `0.05` | Target useful work per dynamic chunk. |
| `minimum_dynamic_chunk_size` | `1` | Lower chunk bound. |
| `maximum_dynamic_chunk_size` | `65,536` | Upper chunk bound. |
| `enable_static_thread_auto_candidates` | `false` | Allows StaticThread in automatic ranking. |
| `execution_engine` | `Auto` | Forces an engine or enables adaptive selection. |

## Learning, calibration, and ranking

The remaining fields control utility-model loading, experience persistence, predictive shadow/control, machine calibration, historical ranking, confidence estimation, hardware-aware prediction, memory models, residual correction, learned override guards, empirical overrides, and safe exploration. Their defaults intentionally keep experimental control paths disabled while enabling bounded analytical corrections.

Important opt-in controls:

| Field | Default | Effect when enabled |
|---|---:|---|
| `enable_utility_model_runtime` | `false` | Loads the configured utility model artifact. |
| `enable_experience_persistence` | `false` | Reads/writes experience across processes. |
| `enable_predictive_shadow` | `false` | Evaluates predictive recommendations without control. |
| `enable_predictive_decisions` | `false` | Allows predictive recommendations to affect execution. |
| `enable_learned_runtime_scaling` | `false` | Enables learned total-work scaling. |
| `enable_online_exploration` | `false` | Permits bounded alternative-plan trials. |

Advanced thresholds should be changed only with a repeatable calibration and holdout workflow. The complete defaults are defined in `include/smart/core/config.hpp`, which remains the source of truth.
