# Configuration

> **Current documentation:** SmartParallel v1.1.0.

Configuration is exposed through `smart::global_config()` in `<smart/core/config.hpp>`. Set configuration before starting concurrent work.

## Common controls

| Field | Purpose |
|---|---|
| `execution_engine` | Select `Auto`, `Sequential`, `ThreadPool`, `StaticThread`, or `OneTbb`. |
| `enable_timing_diagnostics` | Collect phase timing diagnostics. |
| `enable_experience` | Record runtime experience. |
| `enable_parallel_for_auto_profiling` | Profile unknown root callbacks. |
| `enable_parallel_for_profile_cache` | Reuse bounded callsite profiles. |
| `parallel_for_minimum_predicted_speedup` | Minimum predicted advantage before choosing parallel execution. |
| `enable_nested_execution_session` | Enable coordinated root sessions. |
| `nested_root_concurrency_budget` | Root participant budget; zero selects the runtime default. |
| `enable_nested_parallel_frontier` | Enable automatic frontier coordination. |
| `enable_nested_execution_trace` | Record structured nested traces. |
| `enable_parallel_for_backend_calibration` | Permit bounded backend calibration. |
| `enable_experience_persistence` | Enable configured persistence workflow; disabled by default. |

## Nested tuning

Nested tuning fields include minimum iterations/chunks per worker, target chunks per worker, minimum parallel work, chunk-time target, plan hysteresis, frontier deferral/promotion, descendant direct mode, root analytical cold start, pilot cold start, and session-local plan memoization.

Defaults are release-tested. Change them only with workload-specific measurements and the correctness/trace checks described in [diagnostics](diagnostics.md) and [benchmark methodology](benchmark-methodology.md).

## Cache and retention limits

Profiles, experience records, exploration states, backend-calibration states, plan snapshots, and trace records all have configurable bounded limits. Increasing them trades retained memory for longer history.

## Configuration safety

Policy generations help invalidate cached plans between calls, but they do not make concurrent writes to `global_config()` safe. Treat configuration changes as an initialization or quiescent-state operation.
