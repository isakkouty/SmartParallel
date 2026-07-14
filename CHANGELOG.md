
## Phase 6B - Family-specific calibration

- Connected workload-family classification to predictive candidate costs.
- Added bounded, confidence-weighted family corrections.
- Added memory-saturation and worker-pressure diagnostics.
- Added `smartparallel_phase6_family_specific_calibration`.

## V1 development — Phase 3 worker-aware calibration

- Added per-worker backend calibration points for ThreadPool, StaticThread, and oneTBB.
- Added measured compute-like and streaming-like scaling curves.
- Replaced maximum-worker overhead extrapolation with nearest measured worker calibration.
- Added conservative workload-class selection for calibrated speedup estimates.
- Added `smartparallel_phase3_worker_aware_calibration`.
## V1 development — Phase 3 adaptive execution

- Added adaptive worker-count candidate generation.
- Added predictive dynamic chunk sizing.
- Added ThreadPool dynamic work claiming and oneTBB task-arena limits.
- Added chunk size to execution plans and persisted experience keys.
- Added Phase 3 correctness and candidate-generation tests.
- Updated validation to compare exact execution parameters.

# Changelog

All notable project changes should be recorded here.

## Beta 1.0

### Added

- Adaptive `for_each`, `parallel_for`, and `for_each_pair` APIs
- Workload builders, analysis, fingerprints, and hardware characteristics
- Runtime function profiler and profile cache
- Composite decision-provider system and execution reports
- Sequential, static-thread, thread-pool, and oneTBB execution paths
- Timing diagnostics and experience database persistence
- Ten standardized benchmarks
- CSV result export and one-command plot generation
- Root, architecture, API, roadmap, contribution, and benchmark documentation

### Changed

- Reorganized public headers into subsystem directories
- Standardized benchmark console output and CSV schemas
- Separated SmartParallel execution time from framework overhead in benchmark reports

### Known limitations

- Very small workloads expose fixed profiling and decision overhead
- Very cheap memory-streaming operations may be parallelized too early
- Adaptive worker count and grain-size selection are not complete
- Windows is the primary validated platform
## Unreleased — V1 development

- Added deterministic local and distributed profiler sampling.
- Added regional callback-cost observations.
- Added warm-up and steady-state cost separation.
- Added advanced Phase 1 sensor validation.


## V1 development — Phase 1 sensor layer

- Added safe isolated profiling semantics and overflow-safe workload arithmetic.
- Added provenance-aware structural observations and storage topology.
- Added cache-relative scale, reuse, and hardware-relative workload metrics.
- Added adaptive calibrated profiling, bounded sampling, robust statistics,
  spatial observations, and warm-up separation.
- Made structural observations the canonical input for performance-model cache
  pressure.
- Added five focused Phase 1 validation tests and final sensor-layer diagnostics.

## V1 development — Phase 2 predictive engine

- Added explicit candidate execution-plan cost estimates.
- Added predictive total-time, speedup, efficiency, memory, imbalance, scheduling, and framework breakdowns.
- Added predictive shadow mode, enabled by default without changing execution.
- Added opt-in predictive control guarded by a minimum confidence threshold.
- Added predictive diagnostics and focused Phase 2 tests.

## V1 development — prediction feedback and persistence

- Added function-aware execution fingerprints.
- Added prediction-versus-actual experience records.
- Added opt-in persistent experience loading and autosaving.
- Added bounded historical correction of predictive plan costs.
- Added persistence and calibration tests.

## V1 development — predictive validation baseline

- Added a dedicated validation suite that measures every predictive candidate.
- Added winner accuracy, candidate runtime error, and selected-plan regret metrics.
- Added CSV outputs and plotting support for prediction validation.

## V1 development — Phase 2 machine calibration

- Added lazy machine-specific backend runtime calibration for ThreadPool, StaticThread, and oneTBB.
- Predictive scheduling overhead now uses measured backend curves when enabled.
- Added tail-sensitive useful-work estimation for mixed and irregular workloads.
- Validation executables now receive `tbb12.dll` automatically on Windows CMake builds.

## V1 development — Phase 2 holdout validation

- Added a separate holdout validation executable for unseen workload families.
- Added memory-streaming, cache-resident, branch-heavy, tiny-heavy, clustered, sparse-like, large-record, and pair workloads.
- Added exact-winner and within-3%-of-best accuracy metrics.
- Added p90 prediction-error and regret metrics.
- Extended validation plotting with calibration/holdout prefixes and per-family summaries.

## V1 development — Phase 4 experience-aware ranking

- Added an explainable candidate ranker that blends analytical cost ordering
  with stable execution history for the exact workload fingerprint and plan.
- Added ranking diagnostics to every predictive candidate: analytical score,
  historical score, blend weight, sample count, and whether history was used.
- Kept analytical prediction as the cold-start and low-confidence fallback.
- Added a focused Phase 4 candidate-ranking test.

## Phase 4 ranking evolution validation

- Added repeated real-execution validation for experience-aware candidate ranking.
- Added cold-start versus final-round regret metrics.
- Added candidate-level ranking diagnostics per learning round.
- Added save/clear/reload validation for persisted ranking experience.
- Added `smartparallel_phase4_ranking_persistence` regression test.

## Phase 5 — Safe online exploration

Added opt-in bounded exploration of near-best candidates, confidence and score-gap filters, deterministic candidate rotation, harmful-run cooldown, execution diagnostics, and a focused exploration-policy test.

## Phase 6A - Model refinement foundation

- Added a conservative workload-family classifier.
- Added diagnostic confidence and per-family evidence reporting.
- Added explicit provenance flags for hints, structural observations, and profiler observations.
- Registered `smartparallel_phase6_workload_family_classifier` in CTest.
- Added Phase 6A architecture documentation.

## Phase 6C - Uncertainty-aware residual correction

- Added bounded residual correction on top of the analytical predictor.
- Added evidence-weighted uncertainty and exact-history diagnostics.
- Added conservative similarity transfer for residual errors.
- Added `smartparallel_phase6_residual_correction` test.

## Phase 6D — Bounded similarity transfer

- Exposed component-level workload fingerprint similarity diagnostics.
- Centralized similarity calculation for ranking and residual correction.
- Added strict incompatible-kind rejection and bounded transfer validation.
- Added `smartparallel_phase6_similarity_transfer`.
- Documented V1 predictive-model regression gates.

## Model Refinement Step 1

- Made residual prediction correction workload-family aware.
- Added conservative family-specific history weights and correction bounds.
- Switched residual blending to logarithmic space for symmetric relative errors.
- Added candidate diagnostics for effective family residual limits.
- Added `smartparallel_phase7_step1_family_aware_residual`.

## Phase 7 Step 2 — Workload Similarity Normalization

- normalized fingerprint comparison in logarithmic feature space;
- decoded real-valued cost and variation buckets before comparison;
- added similarity evidence coverage and normalized-distance diagnostics;
- missing observations no longer create artificial perfect matches;
- automated oneTBB runtime copying for clean Windows/NMake test builds;
- added `smartparallel_phase7_step2_similarity_normalization`.

## Phase 7 Step 3 — Historical Overconfidence Control

- Added evidence-, stability-, prediction-error-, and recent-consistency-aware history authority.
- Added an immediate guard against recently harmful or contradictory outcomes.
- Added bounded negative-evidence retention for consistently poor plans.
- Added ranking trust diagnostics and a focused regression test.

## Phase 7 Step 4 — Explicit Confidence Model

- Added a unified candidate confidence model.
- Combined profile, family, residual, historical, similarity, and model-agreement evidence.
- Added bounded confidence-aware ranking risk for close, weakly supported choices.
- Added decision-margin confidence and detailed diagnostics.
- Added `smartparallel_phase7_step4_confidence_model` regression test.

## Phase 7 Steps 5-6

- Added enhanced workload fingerprints for access pattern, stride, cache pressure, topology and profile shape.
- Extended similarity transfer to avoid treating equal-size but structurally different workloads as equivalent.
- Added bounded hardware-aware prediction for cache pressure, locality, SMT and NUMA effects.
- Added focused tests and design notes for both refinements.

## Phase 8 Step 1 - memory-bandwidth saturation calibration

- Added a bounded bandwidth-saturation correction for streaming-memory plans.
- Added large-record and bytes-per-iteration evidence to streaming cost estimates.
- Penalized worker counts beyond an estimated hardware-relative saturation point.
- Added per-candidate bandwidth diagnostics and a focused regression test.

## Phase 8 Step 2

- Added specialized cache-resident, bandwidth-bound, latency-bound, and large-record memory calibration.
- Added confidence-bounded regime authority and diagnostics to `PlanCostEstimate`.
- Added `smartparallel_phase8_step2_memory_access_calibration` regression test.

## Phase 8 Step 2 stabilization

- Corrected workload access-pattern semantics: indexed container capability is no longer treated as observed random memory access.
- Index ranges now leave semantic memory randomness unknown.
- Memory-access calibration no longer synthesizes randomness from a previously derived workload-family label.
- Bandwidth-bound correction is delegated exclusively to Phase 8 Step 1 to prevent stacked penalties.
- Cold static memory-access factors are constrained to 0.97-1.05 and apply only to predicted execution cost.
- Removed the second confidence penalty and memory-penalty scaling from Step 2.
- Added production-path regression coverage for `std::vector` and index-range workloads.

## Phase 9 step 1

- Added robust profile-cost calibration to prevent sparse or unreliable mean outliers from dominating full-workload runtime extrapolation.
- Added a focused regression test and documentation.

## Phase 10 — Learned Runtime Scaling

- Added bounded family- and profile-shape-aware profile extrapolation.
- Added runtime scaling exponent, confidence, factor and coverage diagnostics.
- Preserved linear extrapolation as the zero-confidence fallback.
- Added `smartparallel_phase10_learned_runtime_scaling` regression test.
