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


## V1 Phase 3 stabilization

- [x] Adaptive worker-count candidates
- [x] Adaptive dynamic chunk sizing
- [x] Parameter-aware execution
- [x] Worker-aware backend calibration
- [x] Compute-like and streaming-like machine probes
- [ ] Re-run calibration and holdout validation on target hardware
- [ ] Keep predictive control opt-in until regret is acceptable

## V1 Phase 4 — Experience-aware candidate ranking

- [x] Preserve the analytical cost model as the cold-start baseline
- [x] Rank exact candidate plans from accumulated execution history
- [x] Blend history according to sample stability and confidence
- [x] Expose ranking diagnostics for explainability
- [x] Add focused ranking validation
- [ ] Collect repeated real executions for ranking validation
- [ ] Add similarity-aware ranking across nearby workload fingerprints
- [ ] Keep ranking control opt-in through the existing predictive-decision gate

### Phase 4 validation checkpoint

- Repeated candidate exploration and regret-by-round reporting.
- Persistence validation for learned candidate ordering.
- Next: bounded exploration during normal execution and stale-history handling.

## Phase 6 - Model refinement

- [x] 6A: workload-family classifier, diagnostics, and registered validation test
- [ ] 6B: family-specific calibration and memory-aware features
- [ ] 6C: residual correction and uncertainty-aware blending
- [ ] 6D: bounded similarity transfer, regression gates, and V1 model freeze


### Phase 6B — Family-specific calibration
Implemented bounded family-aware cost corrections and diagnostics.

### Phase 6D — Similarity transfer and model freeze gates

- [x] Expose inspectable fingerprint-similarity components.
- [x] Reject incompatible workload kinds.
- [x] Validate bounded residual transfer from nearby workloads.
- [x] Document V1 predictive-model acceptance gates.

### Model refinement — Step 1 complete

Residual prediction correction is now family-aware, confidence-weighted, and
log-space blended. The next focused update normalizes workload similarity.
