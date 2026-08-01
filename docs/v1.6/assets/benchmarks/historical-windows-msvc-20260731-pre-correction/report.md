# SmartParallel v1.6.0 Scientific Foundations — Validation Report

> Performance evidence in this report is machine-specific.

- Raw samples: **3104**
- Compiler: `MSVC 1944`
- OS / architecture: `Windows / x86_64`
- Canonical plan IDs: `canonical-neumaier-v1-leaf1024, canonical-pairwise-v1-leaf1024, canonical-scaled-sumsq-v1-leaf1024`

## Gates

- correctness_pass: **PASS**
- reproducibility_pass: **PASS**
- route_authentication_pass: **PASS**
- numerical_capability_pass: **PASS**
- sum_adversarial Accurate error improvement: **PASS**
- dot_adversarial Accurate error improvement: **PASS**
- Reproducibility matrix: **PASS**
- Fast-mode largest-workload ratio: **1.0335x** (PASS)

## Largest sum workload timing

| Route or policy | Median stable time (ms) |
|---|---:|
| Direct sequential reference | 0.8535 |
| Retained legacy Fast overload | 0.1554 |
| Policy-aware Fast | 0.1606 |
| Reproducible | 0.2076 |
| Accurate | 1.5703 |

## Heat diffusion (20 iterations)

| Policy | Median stable time (ms) |
|---|---:|
| Fast | 129.93 |
| Reproducible | 673.254 |
| Accurate | 685.598 |

## Adversarial numerical error

| Operation | Fast | Reproducible | Accurate |
|---|---:|---:|---:|
| sum_adversarial | 3000 | 3000 | 0 |
| dot_adversarial | 3000 | 3000 | 0 |

## Interpretation

Fast preserves the retained adaptive/native path. Reproducible pays for a fixed leaf decomposition and merge tree. Accurate adds compensated or scaled arithmetic where the operation has a meaningful stronger method. AXPY and stencil Accurate intentionally share the Reproducible pointwise arithmetic contract.

No result here establishes cross-compiler, cross-binary, cross-architecture, safety-critical, hard-real-time, or universal performance guarantees.
