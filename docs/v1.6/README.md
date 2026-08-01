# SmartParallel v1.6.0 — Scientific Foundations

**Trust goal:** **Trust the number.**

SmartParallel v1.6 makes numerical behavior explicit before the framework expands to persistent runtimes or additional external providers. It preserves the adaptive scheduler, nested coordination, v1.4 algorithms, and v1.5 Vision routes while adding deterministic numerical plans, accurate arithmetic for selected operations, host-memory scientific views, and a complete heat-diffusion pilot.

## What v1.6 delivers

- Explicit per-operation `Fast`, `Reproducible`, and `Accurate` policies.
- Fixed, versioned reduction plans whose leaves and merge trees do not depend on worker count or scheduler timing.
- Separate fixed pointwise plans for AXPY and stencil operations, allowing deterministic parallel execution without pretending that pointwise work is a reduction.
- Deterministic compensated sum and dot product.
- Deterministic scaled sum-of-squares norm for improved overflow and underflow behavior.
- Experimental host-only `View<T, Rank>`, `VectorView`, and `MatrixView` types.
- Experimental AXPY, dot, norm, and five-point stencil APIs.
- Backward-compatible adapters between the existing Vision image view and the new rank-2 view vocabulary.
- A readable 2D heat-diffusion pilot using ping-pong matrix views.
- Schema-v2 benchmark evidence with full-output validation, execution authentication, numerical error, raw samples, reports, and nine SVG plots.

## Evidence at a glance

The accepted corrected publication contains **2,442 Linux/GCC/x86-64 samples**. Every execution-validity, required-reference, reproducibility, route-authentication, numerical-capability, cross-scheduler, and pointwise-plan gate passed.

- Accurate adversarial sum error: **3000 → 0**.
- Accurate adversarial dot error: **3000 → 0**.
- Policy-aware Fast versus the retained legacy Fast overload: paired median **1.0634×**, 90% robust interval **0.9739–1.1611×**, classified **inconclusive-pass** because the data does not establish a regression above the 5% investigation boundary.
- The largest Fast AXPY, dot, norm, stencil, and heat workloads passed the performance-sanity gate with machine-specific speedups of **1.19×**, **2.35×**, **2.96×**, **3.78×**, and **2.14×** over direct sequential.
- Reproducible and Accurate AXPY and stencil remained bitwise stable across the tested eligible schedulers and worker budgets while authenticating parallel execution.
- Full AXPY vectors, stencil fields, and heat-diffusion fields were checked outside timed regions and recorded with complete-output digests.

![Numerical error by policy](assets/benchmarks/v1.6.0_numerical_error.svg)

![Cross-scheduler reproducibility matrix](assets/benchmarks/v1.6.0_reproducibility_matrix.svg)

The heat-diffusion result is reported without universal claims: on the accepted AMD EPYC environment, complete-field-validated ThreadPool execution was **2.14× faster in Fast**, **1.98× in Reproducible**, and **2.10× in Accurate** mode than the compact direct-sequential oracle. The evidence demonstrates the corrected kernel on that machine only.

See the [complete benchmark report](benchmarks.md).

## Documentation map

- [Numerical contract](numerical-contract.md)
- [Floating-point environment](floating-point-environment.md)
- [Special-value contract](special-values.md)
- [Execution architecture](architecture.md)
- [Data views and overlap](data-views.md)
- [Scientific operations](scientific-operations.md)
- [Heat-diffusion pilot](heat-diffusion-pilot.md)
- [Benchmark results](benchmarks.md)
- [Benchmark methodology](benchmark-methodology.md)
- [Benchmark reproduction](benchmark-reproduction.md)
- [Validation](validation.md)
- [API maturity](api-maturity.md)
- [Migration guide](migration.md)
- [Known limitations](known-limitations.md)
- [Release notes](release-notes.md)

## Scope boundary

v1.6 is a scientific-computing-oriented adaptive CPU framework with explicit numerical behavior and non-owning multidimensional data foundations. Persistent profiles, owned Runtime instances, deterministic deployment replay, process-wide resource governance, BLAS/FFT providers, OpenMP integration, language bindings, and stable ABI guarantees remain later roadmap work.
