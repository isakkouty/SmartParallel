# SmartParallel v1.7 benchmark analysis

Measurements use repetition-matched pairs and deterministic bootstrap 95% intervals. A result whose interval crosses an objective is marked `INCONCLUSIVE-PASS`; only a complete interval on the wrong side of an objective fails release validation.

## Objective gates

- `explicit_runtime_absolute_overhead_le_20us`: **INCONCLUSIVE-PASS**
- `copied_context_absolute_overhead_le_20us`: **INCONCLUSIVE-PASS**
- `warm_start_speedup_ge_1_5x`: **PASS**
- `deterministic_within_25pct_of_warm`: **PASS**
- `profile_1000_entries_loads_under_1000ms`: **PASS**

## Derived findings

- Adaptive warm start speedup: **2.65×** (95% interval **2.54–2.74×**).
- Deterministic/warm latency ratio: **0.993×** (95% interval **0.937–1.050×**).
- Explicit Runtime paired overhead: **-9.600 µs** (95% interval **-94.200–67.200 µs**).
- Copied ExecutionContext paired overhead: **19.400 µs** (95% interval **-42.800–142.900 µs**).
- The 1,000-entry profile loaded in **750.825 ms** (95% interval **745.505–753.501 ms**, 0.751 ms/entry).

## Median measurements

- `api_overhead/copied_context`: 1.174200 ms (31 samples)
- `api_overhead/explicit_runtime`: 1.164400 ms (31 samples)
- `api_overhead/free_function`: 1.182500 ms (31 samples)
- `calibration/dot`: 4.519950 ms (2 samples)
- `calibration/norm`: 6.050050 ms (2 samples)
- `calibration/stencil_2d`: 7.378050 ms (2 samples)
- `profile_load/1`: 0.855900 ms (31 samples)
- `profile_load/10`: 7.276700 ms (31 samples)
- `profile_load/100`: 72.212200 ms (31 samples)
- `profile_load/1000`: 750.825000 ms (31 samples)
- `runtime_construction/no_profile`: 0.051900 ms (31 samples)
- `runtime_construction/small_profile`: 5.512800 ms (31 samples)
- `startup/adaptive_cold`: 3.075100 ms (31 samples)
- `startup/adaptive_warm`: 1.173100 ms (31 samples)
- `startup/deterministic`: 1.186100 ms (31 samples)
- `warm_start_operation/dot`: 0.484800 ms (31 samples)
- `warm_start_operation/norm`: 1.221200 ms (31 samples)
- `warm_start_operation/stencil_2d`: 0.138900 ms (31 samples)

The benchmark executable independently asserts that every cold sample comes from a fresh Adaptive Runtime, every warm sample authenticates a loaded profile, Deterministic execution performs no learning samples, adaptive timing probes, or profile mutations, and all three startup variants produce identical output. Cross-process identity and v1.6 regression status are validated by separate release-script stages.
