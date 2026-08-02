# SmartParallel v1.8 — Accepted benchmark evidence

This page records the accepted Linux/GCC publication for the hardened v1.8 CPU-governance release. The evidence directory preserves the schema-v2 raw CSV, environment record, statistical metrics, human-readable report, fourteen SVG figures, and a plot manifest tying every figure to the raw-data SHA-256.

## Acceptance boundary

The portable release claims are correctness and resource-safety properties:

- governor-native participation remained at or below the declared budget;
- the ungoverned control produced real participating execution above effective CPU capacity;
- Adaptive admission retained distinct minimum, preferred, maximum, and granted worker fields;
- the oldest blocked exact request completed after no more than four bounded bypasses;
- nested execution did not expand the parent grant;
- deterministic insufficient-budget failure occurred before output modification;
- pending cancellation woke the queue directly;
- every raw benchmark record authenticated correctness.

All nine mandatory benchmark gates passed. Performance comparisons are machine-specific and are not converted into release correctness claims.

## Accepted Linux/GCC results

The publication used 31 repetitions, two untimed warmups per paired pressure condition, alternating governed/ungoverned order, and a paired bootstrap with 10,000 resamples at 95% confidence.

| Measurement | Accepted result |
|---|---:|
| Effective CPU capacity | 4 participants |
| Declared governor budget | 4 participants |
| Governed peak participation | 4 maximum |
| Ungoverned peak participation | 6 maximum |
| Uncontended acquire/release | 0.146248 µs [0.145617, 0.146789] |
| Oldest large-request completion rank | 5 in all 31 repetitions |
| Configured bounded bypass limit | 4 |
| Oldest large-request wait | median 224.134 µs; p95 306.366 µs |
| Governed/ungoverned throughput ratio | 0.771707 [0.677117, 0.850243] — **FAIL** |
| Governed/ungoverned p95-latency ratio | 1.29830 [1.17206, 1.44233] — **FAIL** |
| Governed/ungoverned completion-balance ratio | 1.69773 [1.46891, 1.73482] — **FAIL** |

The negative results mean this particular short CPU-pressure workload completed faster and with better completion balance when ungoverned. v1.8 does not hide that outcome. The demonstrated value in this publication is bounded participation, exact deterministic admission, nested safety, direct cancellation, starvation-resistant queue behavior, and explainability—not an unconditional speedup.

## Participation and real machine pressure

![Declared budget versus peak observed participation](assets/benchmarks/linux-gcc-accepted/01_budget_vs_peak_participation.svg)

![Real machine-pressure participation](assets/benchmarks/linux-gcc-accepted/12_true_machine_oversubscription.svg)

## Throughput and latency

![Governed versus ungoverned throughput with 95% confidence interval](assets/benchmarks/linux-gcc-accepted/02_throughput_ratio_95ci.svg)

![Completion latency percentiles](assets/benchmarks/linux-gcc-accepted/03_completion_latency_percentiles.svg)

![Lease wait-time ECDF](assets/benchmarks/linux-gcc-accepted/04_lease_wait_ecdf.svg)

## Multi-Runtime admission and fairness

![Multi-Runtime scaling](assets/benchmarks/linux-gcc-accepted/05_multi_runtime_scaling.svg)

![Admission fairness wait distributions](assets/benchmarks/linux-gcc-accepted/06_completion_fairness.svg)

![Oldest large-request duration and bounded completion rank](assets/benchmarks/linux-gcc-accepted/07_oldest_waiter_duration.svg)

The fairness gate is deliberately separate from the governed-versus-ungoverned workload-completion balance metric. The queue gate proves bounded bypass and eventual admission; it does not promise equal application completion times.

## Admission overhead and partial grants

![Governor acquisition overhead](assets/benchmarks/linux-gcc-accepted/08_governor_overhead.svg)

![Adaptive operation-specific partial grant](assets/benchmarks/linux-gcc-accepted/09_adaptive_partial_grant.svg)

## Nested execution and schedulers

![Nested participation versus parent grant](assets/benchmarks/linux-gcc-accepted/10_nested_participation.svg)

![Sequential, ThreadPool, StaticThread, and available scheduler comparison](assets/benchmarks/linux-gcc-accepted/11_scheduler_comparison.svg)

## Retained regression and deterministic admission

![Retained measured v1.6–v1.7 regression ratios](assets/benchmarks/linux-gcc-accepted/13_v15_v17_regression_ratios.svg)

![Deterministic exact-grant success and fail-closed rejection](assets/benchmarks/linux-gcc-accepted/14_deterministic_exact_grant.svg)

## Cross-platform publication state

The retained figures on this page are Linux/GCC results only. A cross-platform comparison figure is generated only when a second platform raw dataset is supplied to the analyzer. Windows/MSVC evidence must be produced and accepted separately before the release is tagged final; no placeholder Windows comparison is included.
