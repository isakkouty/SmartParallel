# SmartParallel v1.6.0 Scientific Foundations — Validation Report

> Performance evidence is machine-specific. Correctness fields are based on full-output validation outside timed regions.

- Raw samples: **3936**
- Evidence schema: **2**
- Compiler: `MSVC 1944`
- OS / architecture: `Windows / x86_64`
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
- Fast-mode paired ratio: **1.0000x** (90% robust interval 0.9764–1.0242x; PASS)

## Evidence semantics

- `execution_valid` checks expected finite/NaN/infinity classifications.
- `reference_accuracy_pass` checks the independent numerical reference.
- Fast and Reproducible are allowed to expose cancellation error on the deliberate adversarial cases; Accurate must pass the reference there.
- AXPY, stencil, and heat diffusion are validated over every logical output element and recorded with full-field digests.
- Fast compatibility uses adjacent alternating call pairs and a median/MAD 90% interval; it fails only when the interval's lower bound exceeds the 5% investigation threshold.

## Largest sum workload timing

| Route or policy | Median stable time (ms) |
|---|---:|
| Direct sequential reference | 0.8519 |
| Retained legacy Fast overload | 0.151 |
| Policy-aware Fast | 0.1547 |
| Reproducible | 0.1721 |
| Accurate | 1.1878 |

## Heat diffusion — 20 iterations

| Policy | Median stable time (ms) | Scheduler evidence | Speed vs direct sequential |
|---|---:|---|---:|
| Fast | 95.8688 | ThreadPool | 0.091x |
| Reproducible | 92.0246 | ThreadPool | 0.095x |
| Accurate | 89.9881 | ThreadPool | 0.097x |
| Direct sequential | 8.6987 | DirectSequential | 1.000x |

## Adversarial numerical error

| Operation | Fast | Reproducible | Accurate |
|---|---:|---:|---:|
| dot_adversarial | 3000 | 3000 | 0 |
| sum_adversarial | 3000 | 3000 | 0 |

## Interpretation

Reproducible and Accurate reductions use fixed leaves and fixed merge trees. Reproducible and Accurate pointwise operations use worker-independent fixed pointwise tiles; they preserve each element's expression order while allowing eligible schedulers to execute tiles concurrently.

Accurate AXPY and stencil intentionally share the Reproducible pointwise arithmetic contract because no stronger operation-specific arithmetic method is promised for those operations.

No result here establishes cross-compiler, cross-binary, cross-architecture, safety-critical, hard-real-time, or universal performance guarantees.
