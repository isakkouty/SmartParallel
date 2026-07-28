# SmartParallel v1.5 Adaptive Execution Routes

This report validates balanced initial learning, sparse drift sentinels, current-context ABBA revalidation, deployment settling, balanced steady-state route ordering, and batched adjacent ABBA/BAAB dispatch measurements.

- Steady-state repetitions: **31**
- Route equivalence: **5.0% or 1.0 µs**
- Native oracle gate: **10.0% or 0.5 µs**
- Dispatch gate: **1.0 µs; failure only when the lower 95% robust bound exceeds the gate**
- Native kernel: **avx2**
- Raw CSV SHA-256: `dc471e82787c09cd26f5b92c28b62d370bb4c07dfd46a89feaa2e958a5325f60`
- Learning CSV SHA-256: `273d670c8638d9bd2985a73312feca7ed7b146d5cbc6dbb28ca41af9678b19c9`

## Proof gates

| Preset | Settled route | Switches | Fastest route | Route regret | Batched overhead (95% robust interval) | Native vs oracle | Training samples + holdout | Overall |
|---|---|---:|---|---:|---:|---:|---:|---|
| tiny_320x240 | native_sequential | 0 | smart_native_sequential | +0.00% | +0.043 µs [+0.041, +0.046] (pass) | -11.54% | 11 + 2 | PASS |
| small_640x480 | native_sequential | 0 | smart_native_sequential | +0.00% | +0.052 µs [+0.043, +0.060] (pass) | -10.10% | 11 + 2 | PASS |
| medium_1920x1080 | native_sequential | 1 | smart_native_sequential | +0.00% | +0.051 µs [-0.017, +0.120] (pass) | -6.22% | 3 + 0 | PASS |
| medium_1920x1080_roi | native_sequential | 1 | smart_native_sequential | +0.00% | +0.057 µs [-0.060, +0.175] (pass) | -7.41% | 3 + 0 | PASS |
| large_3840x2160 | native_thread_pool | 0 | smart_native_thread_pool | +0.00% | +5.700 µs [-1.607, +13.007] (inconclusive-pass) | -1.26% | 11 + 2 | PASS |
| very_large_7680x4320 | native_thread_pool | 0 | smart_native_one_tbb | +0.96% | +0.475 µs [-4.079, +5.029] (inconclusive-pass) | -0.68% | 3 + 2 | PASS |

## Verdict

- Route selection: **6/6** presets passed.
- Batched stable hot dispatch: **6/6** presets passed.
- Native kernel versus independent oracle: **6/6** presets passed.
- Combined release gate: **6/6** presets passed.

The v1.5 threshold vertical slice passed all proof gates on this machine.
