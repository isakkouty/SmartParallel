# SmartParallel v1.6.0 Scientific Foundations — Validation Report

> Performance evidence is machine-specific. Correctness fields are based on full-output validation outside timed regions.

- Raw samples: **2442**
- Evidence schema: **2**
- Compiler: `GCC 14.2`
- OS / architecture: `Linux / x86_64`
- Execution-plan IDs: `canonical-neumaier-v1-leaf1024, canonical-pairwise-v1-leaf1024, canonical-pointwise-2d-v1-target4096, canonical-pointwise-v1-target4096, canonical-scaled-sumsq-v1-leaf1024`

## Release gates

- execution_valid: **PASS**
- reproducibility: **PASS**
- route_authentication: **PASS**
- numerical_capability: **PASS**
- required_reference_accuracy: **PASS**
- dot_adversarial Accurate reference and improvement: **PASS**
- sum_adversarial Accurate reference and improvement: **PASS**
- sum_scaling cross-scheduler bitwise matrix: **PASS**
- axpy_pointwise_matrix cross-scheduler bitwise matrix: **PASS**
- stencil_pointwise_matrix cross-scheduler bitwise matrix: **PASS**
- Pointwise-plan authentication: **PASS**
- Scientific-kernel performance sanity (all largest Fast workloads at least 0.50x direct sequential): **PASS**
- Fast-mode paired ratio: **1.0634x** (90% robust interval 0.9739–1.1611x; NOT-ESTABLISHED)

## Evidence semantics

- `execution_valid` checks expected finite/NaN/infinity classifications.
- `reference_accuracy_pass` checks the independent numerical reference.
- Fast and Reproducible are allowed to expose cancellation error on the deliberate adversarial cases; Accurate must pass the reference there.
- AXPY, stencil, and heat diffusion are validated over every logical output element and recorded with full-field digests.
- Fast compatibility uses adjacent alternating call pairs and a median/MAD 90% interval; it fails only when the interval's lower bound exceeds the 5% investigation threshold.

## Largest sum workload timing

| Route or policy | Median stable time (ms) |
|---|---:|
| Direct sequential reference | 0.855579 |
| Retained legacy Fast overload | 0.292343 |
| Policy-aware Fast | 0.36412 |
| Reproducible | 0.291712 |
| Accurate | 1.52213 |

## Scientific-kernel performance sanity

The sanity gate is deliberately broad: for the largest tested workload, Fast execution must remain at least 0.5x the compact direct-sequential reference. It is a regression detector, not a universal speedup claim.

| Operation | SmartParallel Fast (ms) | Direct sequential (ms) | Speed vs direct |
|---|---:|---:|---:|
| axpy | 0.370569 | 0.440944 | 1.190x |
| dot | 0.36424 | 0.854567 | 2.346x |
| norm | 0.287556 | 0.852264 | 2.964x |
| stencil_2d | 0.3705 | 1.40018 | 3.779x |
| heat_diffusion_20 | 2.3095 | 4.9479 | 2.142x |

## Heat diffusion — 20 iterations

| Policy | Median stable time (ms) | Scheduler evidence | Speed vs direct sequential |
|---|---:|---|---:|
| Fast | 2.3095 | ThreadPool | 2.142x |
| Reproducible | 2.5049 | ThreadPool | 1.975x |
| Accurate | 2.35622 | ThreadPool | 2.100x |
| Direct sequential | 4.9479 | DirectSequential | 1.000x |

## Adversarial numerical error

| Operation | Fast | Reproducible | Accurate |
|---|---:|---:|---:|
| dot_adversarial | 3000 | 3000 | 0 |
| sum_adversarial | 3000 | 3000 | 0 |

## Interpretation

Reproducible and Accurate reductions use fixed leaves and fixed merge trees. Reproducible and Accurate pointwise operations use worker-independent fixed pointwise tiles; they preserve each element's expression order while allowing eligible schedulers to execute tiles concurrently.

Accurate AXPY and stencil intentionally share the Reproducible pointwise arithmetic contract because no stronger operation-specific arithmetic method is promised for those operations.

No result here establishes cross-compiler, cross-binary, cross-architecture, safety-critical, hard-real-time, or universal performance guarantees.
