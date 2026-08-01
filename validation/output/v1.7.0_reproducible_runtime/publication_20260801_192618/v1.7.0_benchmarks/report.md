# SmartParallel v1.7 benchmark analysis

Measurements use repetition-matched pairs and deterministic bootstrap 95% intervals. A result whose interval crosses an objective is marked `INCONCLUSIVE-PASS`; only a complete interval on the wrong side of an objective fails release validation.

## Objective gates

- `explicit_runtime_absolute_overhead_le_20us`: **INCONCLUSIVE-PASS**
- `copied_context_absolute_overhead_le_20us`: **INCONCLUSIVE-PASS**
- `warm_start_speedup_ge_1_5x`: **PASS**
- `deterministic_within_25pct_of_warm`: **PASS**
- `profile_1000_entries_loads_under_1000ms`: **PASS**

## Derived findings

- Adaptive warm start speedup: **2.60×** (95% interval **2.50–2.76×**).
- Deterministic/warm latency ratio: **1.014×** (95% interval **0.959–1.084×**).
- Explicit Runtime paired overhead: **9.900 µs** (95% interval **-52.400–27.800 µs**).
- Copied ExecutionContext paired overhead: **-0.100 µs** (95% interval **-42.100–28.400 µs**).
- The 1,000-entry profile loaded in **713.304 ms** (95% interval **708.684–716.005 ms**, 0.713 ms/entry).

## Median measurements

- `api_overhead/copied_context`: 1.173800 ms (31 samples)
- `api_overhead/explicit_runtime`: 1.170800 ms (31 samples)
- `api_overhead/free_function`: 1.151000 ms (31 samples)
- `calibration/dot`: 4.490350 ms (2 samples)
- `calibration/norm`: 6.059750 ms (2 samples)
- `calibration/stencil_2d`: 6.688900 ms (2 samples)
- `profile_load/1`: 0.820100 ms (31 samples)
- `profile_load/10`: 6.879400 ms (31 samples)
- `profile_load/100`: 67.734500 ms (31 samples)
- `profile_load/1000`: 713.304000 ms (31 samples)
- `runtime_construction/no_profile`: 0.062100 ms (31 samples)
- `runtime_construction/small_profile`: 5.581900 ms (31 samples)
- `startup/adaptive_cold`: 3.005500 ms (31 samples)
- `startup/adaptive_warm`: 1.163100 ms (31 samples)
- `startup/deterministic`: 1.190800 ms (31 samples)
- `warm_start_operation/dot`: 0.430800 ms (31 samples)
- `warm_start_operation/norm`: 1.200300 ms (31 samples)
- `warm_start_operation/stencil_2d`: 0.132800 ms (31 samples)

The benchmark executable independently asserts that every cold sample comes from a fresh Adaptive Runtime, every warm sample authenticates a loaded profile, Deterministic execution performs no learning samples, adaptive timing probes, or profile mutations, and all three startup variants produce identical output. Cross-process identity and v1.6 regression status are validated by separate release-script stages.
