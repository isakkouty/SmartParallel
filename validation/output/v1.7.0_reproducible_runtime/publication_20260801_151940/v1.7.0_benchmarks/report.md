# SmartParallel v1.7 benchmark analysis

Measurements use repetition-matched pairs and deterministic bootstrap 95% intervals. A result whose interval crosses an objective is marked `INCONCLUSIVE-PASS`; only a complete interval on the wrong side of an objective fails release validation.

## Objective gates

- `explicit_runtime_absolute_overhead_le_20us`: **INCONCLUSIVE-PASS**
- `copied_context_absolute_overhead_le_20us`: **INCONCLUSIVE-PASS**
- `warm_start_speedup_ge_1_5x`: **PASS**
- `deterministic_within_25pct_of_warm`: **PASS**
- `profile_1000_entries_loads_under_1000ms`: **PASS**

## Derived findings

- Adaptive warm start speedup: **2.51×** (95% interval **2.21–2.70×**).
- Deterministic/warm latency ratio: **0.990×** (95% interval **0.968–1.113×**).
- Explicit Runtime paired overhead: **-11.800 µs** (95% interval **-68.200–84.100 µs**).
- Copied ExecutionContext paired overhead: **-39.600 µs** (95% interval **-92.200–43.600 µs**).
- The 1,000-entry profile loaded in **719.595 ms** (95% interval **715.767–729.502 ms**, 0.720 ms/entry).

## Median measurements

- `api_overhead/copied_context`: 1.221000 ms (31 samples)
- `api_overhead/explicit_runtime`: 1.231900 ms (31 samples)
- `api_overhead/free_function`: 1.287500 ms (31 samples)
- `calibration/dot`: 4.556750 ms (2 samples)
- `calibration/norm`: 6.129250 ms (2 samples)
- `calibration/stencil_2d`: 6.980950 ms (2 samples)
- `profile_load/1`: 0.840300 ms (31 samples)
- `profile_load/10`: 7.455300 ms (31 samples)
- `profile_load/100`: 72.728500 ms (31 samples)
- `profile_load/1000`: 719.595000 ms (31 samples)
- `runtime_construction/no_profile`: 0.072600 ms (31 samples)
- `runtime_construction/small_profile`: 6.010300 ms (31 samples)
- `startup/adaptive_cold`: 3.137900 ms (31 samples)
- `startup/adaptive_warm`: 1.321700 ms (31 samples)
- `startup/deterministic`: 1.334900 ms (31 samples)
- `warm_start_operation/dot`: 0.496100 ms (31 samples)
- `warm_start_operation/norm`: 1.240200 ms (31 samples)
- `warm_start_operation/stencil_2d`: 0.142700 ms (31 samples)

The benchmark executable independently asserts that every cold sample comes from a fresh Adaptive Runtime, every warm sample authenticates a loaded profile, Deterministic execution performs no learning samples, adaptive timing probes, or profile mutations, and all three startup variants produce identical output. Cross-process identity and v1.6 regression status are validated by separate release-script stages.
