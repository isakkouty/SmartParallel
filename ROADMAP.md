# Roadmap

## Beta 1.0 — current

Beta 1.0 establishes the architecture and validates the idea across a broad CPU benchmark suite.

- adaptive public algorithms
- workload building and analysis
- runtime function profiling
- composable decision providers
- sequential, static-thread, thread-pool, and oneTBB paths
- timing diagnostics and experience recording
- standardized benchmark CSVs and generated figures
- user and contributor documentation

## Stable 1.0

The goal of stable 1.0 is not to add many new backends. It is to make the current framework dependable as a public library.

- stabilize naming and public headers
- add automated unit and integration tests
- validate Debug and Release builds
- add CI for supported compilers
- test on additional machines
- define a supported-platform matrix
- improve CMake installation and package consumption
- document compatibility and semantic versioning policy
- close known correctness and benchmark methodology issues

## Version 2 — smarter CPU decisions

### Runtime prediction

Estimate total time for each candidate plan, including profiling, scheduling, and execution cost, then select the lowest predicted total.

### Memory behavior

Add explicit signals for compute-bound, memory-bound, branch-heavy, and irregular workloads. The memory-bandwidth benchmark is the main motivating case.

### Adaptive resources

- choose worker count instead of always using all logical threads
- choose static or dynamic scheduling from measured variance
- select chunk or grain size from iteration cost and irregularity

### Better profiling

- warm-up samples
- median and trimmed means
- outlier rejection
- confidence intervals
- adaptive sample counts
- profile caching with clearer invalidation rules

### Hardware awareness

- physical versus logical core policies
- cache/topology-aware decisions
- hybrid-core awareness
- CPU affinity experiments
- NUMA-aware allocation and scheduling research

### Experience system

- richer workload and hardware fingerprints
- persistent versioned storage
- similarity matching and interpolation
- confidence-aware reuse
- regression protection

## Later research

Potential later directions include OpenMP or `std::execution` backends, GPU feasibility studies, heterogeneous execution, and online plan adaptation. These are not Beta 1.0 capabilities and are not promised for the first stable release.
