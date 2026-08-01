# SmartParallel v1.6.0 release notes — Scientific Foundations

SmartParallel v1.6 introduces explicit numerical contracts and the first reusable scientific-data and operation layer while preserving every validated v1.0–v1.5 execution path.

**Release title:** SmartParallel v1.6.0 — Scientific Foundations  
**Trust statement:** **v1.6 — Trust the number.**

## Highlights

- Per-operation `Fast`, `Reproducible`, and `Accurate` policies.
- Worker-independent canonical reduction leaves and merge trees.
- Separate worker-independent pointwise tiles for AXPY and stencil.
- Deterministic compensated sum and dot product.
- Deterministic scaled sum-of-squares norm.
- Host-only `View<T, Rank>`, vector views, and matrix views.
- Experimental AXPY, dot, norm, and five-point stencil APIs.
- Backward-compatible Vision view adapters.
- Complete 2D heat-diffusion pilot.
- Schema-v2 evidence with full-output validation and nine generated SVG plots.
- Reproducible source ZIP tooling with normalized timestamps, stable file order, source-only filtering, and Windows command-file validation.
- Validated pointer/stride scientific kernels that retain entry-time view safety while removing per-element checked-index overhead.
- A largest-workload scientific-kernel performance-sanity gate for AXPY, dot, norm, stencil, and heat diffusion.

## Corrected accepted evidence

The accepted Linux/GCC/x86-64 publication contains **2,442 raw samples**.

- execution validity: **Pass**;
- required reference accuracy: **Pass**;
- reproducibility: **Pass**;
- route authentication: **Pass**;
- numerical capability: **Pass**;
- sum, AXPY, and stencil cross-scheduler matrices: **Pass**;
- pointwise plan authentication: **Pass**;
- Accurate adversarial sum error: **3000 → 0**;
- Accurate adversarial dot error: **3000 → 0**;
- policy-aware Fast / retained Fast: paired median **1.0634×**, 90% robust interval **0.9739–1.1611×**, **inconclusive-pass**;
- largest Fast AXPY, dot, norm, stencil, and heat speedups versus direct sequential: **1.19×**, **2.35×**, **2.96×**, **3.78×**, and **2.14×**;
- corrected deterministic regression suite: **20/20 passed**.

The benchmark validates complete AXPY vectors, complete stencil fields, and complete heat-diffusion fields outside timed regions. It separately reports execution validity and reference accuracy.

The previous Windows schema-v1 evidence remains historical pre-correction material. A later Windows/MSVC schema-v2 run is also retained historically: 3,936 samples, every numerical gate, 20/20 main tests, 20/20 isolated no-oneTBB/no-OpenCV tests, documentation validation, and both installed consumers passed. It predates the validated pointer/stride kernel correction, so its performance values are not current release claims.

The final release workflow also corrects the Windows validation-script defect observed after the dependency-enabled matrix: the no-oneTBB stage no longer uses a fragile `call :label` subroutine, all shipped `.bat`/`.cmd` files use CRLF, and the isolated matrix explicitly unsets vcpkg/toolchain environment state before configuration.

## Honest performance boundary

The final corrected publication removes a preventable hot-loop cost: scientific operations validate views once, then use validated pointer/stride kernels. The accepted machine now shows the largest Fast AXPY, dot, norm, stencil, and heat workloads above the compact direct-sequential references, and all pass the broad 0.5× performance-sanity threshold. These are retained machine-specific observations, not a claim that SmartParallel is universally faster on every processor, compiler, workload, or competing implementation.

## Compatibility

- Existing overloads remain source-compatible and default to Fast.
- v1.4 algorithms, hot dispatch, nested execution, and scheduler behavior remain intact.
- v1.5 Vision routes remain optional and OpenCV remains isolated.
- Exported CMake target names remain unchanged.

## Deliberately deferred

Sort, scan, public 3D stencil, persistent profiles, owned Runtime instances, deterministic deployment replay, process-wide governance, OpenMP, BLAS, FFT, NUMA, GPU, MPI, C/Python bindings, stable ABI guarantees, and safety certification are outside v1.6.
