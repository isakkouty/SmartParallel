# SmartParallel v1.7 benchmark analysis

Measurements use repetition-matched pairs and deterministic bootstrap 95% intervals. A result whose interval crosses an objective is marked `INCONCLUSIVE-PASS`; only a complete interval on the wrong side of an objective fails release validation.

## Objective gates

- `explicit_runtime_absolute_overhead_le_20us`: **INCONCLUSIVE-PASS**
- `copied_context_absolute_overhead_le_20us`: **INCONCLUSIVE-PASS**
- `warm_start_speedup_ge_1_5x`: **PASS**
- `deterministic_within_25pct_of_warm`: **PASS**
- `profile_1000_entries_loads_under_1000ms`: **PASS**

## Derived findings

- Adaptive warm start speedup: **2.57×** (95% interval **2.38–2.67×**).
- Deterministic/warm latency ratio: **1.001×** (95% interval **0.940–1.074×**).
- Explicit Runtime paired overhead: **9.800 µs** (95% interval **-19.800–45.900 µs**).
- Copied ExecutionContext paired overhead: **-1.300 µs** (95% interval **-22.900–36.500 µs**).
- The 1,000-entry profile loaded in **716.908 ms** (95% interval **713.716–719.466 ms**, 0.717 ms/entry).

## Median measurements

- `api_overhead/copied_context`: 1.174600 ms (31 samples)
- `api_overhead/explicit_runtime`: 1.175600 ms (31 samples)
- `api_overhead/free_function`: 1.180000 ms (31 samples)
- `calibration/dot`: 4.515550 ms (2 samples)
- `calibration/norm`: 6.121900 ms (2 samples)
- `calibration/stencil_2d`: 6.523100 ms (2 samples)
- `profile_load/1`: 0.837200 ms (31 samples)
- `profile_load/10`: 7.277000 ms (31 samples)
- `profile_load/100`: 67.939300 ms (31 samples)
- `profile_load/1000`: 716.908000 ms (31 samples)
- `runtime_construction/no_profile`: 0.051800 ms (31 samples)
- `runtime_construction/small_profile`: 5.301300 ms (31 samples)
- `startup/adaptive_cold`: 2.971900 ms (31 samples)
- `startup/adaptive_warm`: 1.156500 ms (31 samples)
- `startup/deterministic`: 1.189900 ms (31 samples)
- `warm_start_operation/dot`: 0.460000 ms (31 samples)
- `warm_start_operation/norm`: 1.198600 ms (31 samples)
- `warm_start_operation/stencil_2d`: 0.130800 ms (31 samples)

The benchmark executable independently asserts that every cold sample comes from a fresh Adaptive Runtime, every warm sample authenticates a loaded profile, Deterministic execution performs no learning samples, adaptive timing probes, or profile mutations, and all three startup variants produce identical output. Cross-process identity and v1.6 regression status are validated by separate release-script stages.
