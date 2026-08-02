# SmartParallel v1.7 benchmark analysis

Measurements use repetition-matched pairs and deterministic bootstrap 95% intervals. A result whose interval crosses an objective is marked `NOT-ESTABLISHED`; only a complete interval on the wrong side of an objective fails release validation.

## Objective gates

- `explicit_runtime_absolute_overhead_le_20us`: **NOT-ESTABLISHED**
- `copied_context_absolute_overhead_le_20us`: **NOT-ESTABLISHED**
- `warm_start_speedup_ge_1_5x`: **PASS**
- `deterministic_within_25pct_of_warm`: **PASS**
- `profile_1000_entries_loads_under_1000ms`: **PASS**

## Derived findings

- Adaptive warm start speedup: **2.29×** (95% interval **2.15–2.46×**).
- Deterministic/warm latency ratio: **0.963×** (95% interval **0.940–1.087×**).
- Explicit Runtime paired overhead: **-10.927 µs** (95% interval **-31.778–31.937 µs**).
- Copied ExecutionContext paired overhead: **16.124 µs** (95% interval **-21.591–30.316 µs**).
- The 1,000-entry profile loaded in **154.696 ms** (95% interval **153.897–155.395 ms**, 0.155 ms/entry).

## Median measurements

- `api_overhead/copied_context`: 0.231103 ms (31 samples)
- `api_overhead/explicit_runtime`: 0.232876 ms (31 samples)
- `api_overhead/free_function`: 0.228910 ms (31 samples)
- `calibration/dot`: 1.236005 ms (2 samples)
- `calibration/norm`: 1.897430 ms (2 samples)
- `calibration/stencil_2d`: 2.478825 ms (2 samples)
- `profile_load/1`: 0.163313 ms (31 samples)
- `profile_load/10`: 1.386500 ms (31 samples)
- `profile_load/100`: 13.624500 ms (31 samples)
- `profile_load/1000`: 154.696000 ms (31 samples)
- `runtime_construction/no_profile`: 0.031948 ms (31 samples)
- `runtime_construction/small_profile`: 1.103580 ms (31 samples)
- `startup/adaptive_cold`: 0.795912 ms (31 samples)
- `startup/adaptive_warm`: 0.349539 ms (31 samples)
- `startup/deterministic`: 0.335127 ms (31 samples)
- `warm_start_operation/dot`: 0.391501 ms (31 samples)
- `warm_start_operation/norm`: 0.658257 ms (31 samples)
- `warm_start_operation/stencil_2d`: 0.103394 ms (31 samples)

The benchmark executable independently asserts that every cold sample comes from a fresh Adaptive Runtime, every warm sample authenticates a loaded profile, Deterministic execution performs no learning samples, adaptive timing probes, or profile mutations, and all three startup variants produce identical output. Cross-process identity and v1.6 regression status are validated by separate release-script stages.
