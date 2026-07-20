
## v1.1.0 Step 6 - Budget-Aware Nested Parallelism

- Added an explicit `BudgetLimitedDelegation` nested policy.
- Compatible native runtimes delegate normally when the requested budget fits the parent budget.
- Oversized compatible requests remain parallel with the inherited parent budget.
- Nested execution falls back to sequential when the parent budget is exhausted or runtimes are incompatible.
- Added a focused Windows build-and-run example for all three budget thresholds.
# Changelog

## Step 5 - Nested Concurrency Budget

- Added `ExecutionContext::concurrency_budget`.
- Added parent, requested, and effective budget reporting to `NestedExecutionDecision`.
- Native nested runtimes inherit a budget capped by the parent context.
- Sequential and fallback execution use a budget of one.
- Added a focused Windows example script for budget inheritance and clamping.


## Unreleased - v1.1.0

- Added deterministic nested runtime policy selection.
- Same-runtime oneTBB nesting delegates to oneTBB natively.
- Unsupported and cross-runtime nesting currently uses a safe sequential inner fallback.
- Execution contexts now expose the active engine, parallel state, and nested policy.



## 1.0.0 packaging hotfix

- Restored automatic vcpkg toolchain discovery from `VCPKG_ROOT` before `project()`.
- Restored the original Windows default triplet, `x64-windows`, when no triplet is supplied.
- Preset builds no longer require repeating `CMAKE_TOOLCHAIN_FILE` or `VCPKG_TARGET_TRIPLET`.

## Release status: Stable v1.0.0.

- Stabilized adaptive index-range `parallel_for`.
- Added automatic callback profiling, profile caching, and a confirmed sequential fast path.
- Added ThreadPool, StaticThread, and oneTBB execution engines.
- Added hardware-, memory-, family-, confidence-, experience-, and residual-aware decision modeling.
- Added deterministic validation, hardening, overhead, OpenCV, scientific, stress, and decision-quality suites.
- Archived beta documentation and replaced it with an authoritative v1 documentation set.
- Organized Windows entry-point scripts under `scripts/`.
- Added benchmark plotting and committed a documented benchmark-results snapshot.

Performance results are machine-specific; see `docs/v1/benchmark-results.md`.

## Build and packaging infrastructure

- Added modular CMake files for the library, examples, tests, validation, and benchmark suites.
- Added `CMakePresets.json` with `debug`, `release`, `examples`, `validation`, `benchmarks`, and `all` presets.
- Added umbrella build options while retaining all previous fine-grained options.
- Added standard install rules and the exported `SmartParallel::smart_parallel` target.
- Added `SmartParallelConfig.cmake` and compatible package-version generation.
- Added automatic oneTBB dependency discovery for installed consumers.
- Added a generated public version header.
- Centralized compiler warnings and Windows oneTBB runtime copying in reusable CMake helpers.
- Expanded build, installation, and downstream-consumer documentation.
