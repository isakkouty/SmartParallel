# Current Phase

SmartParallel is collecting production-safe DecisionContext data for utility learning.

Current status:

- Production decisions do not use the legacy runtime predictor.
- Legacy runtime estimates are retained only in offline validation datasets as a frozen comparison baseline.
- Utility-model training requires at least 100 calibration workloads.
- With fewer than 100 groups, the launcher audits the dataset and reports `MODEL NOT TRAINED`.
- Promotion requires lower holdout mean regret than the offline baseline and no increase in decisions with more than 20% regret.

Run the complete workflow with:

```bat
run_v1_phase1.bat
```

## Latest implementation step: deployable model artifact

The Phase 1 utility learner now saves a versioned `.spm` artifact, and the C++ runtime includes validated save/load and feature-scaling support. This completes the persistence foundation; the next V1 step is wiring an approved artifact into a hybrid runtime decision provider and then validating it in three existing projects.

## V1 hybrid runtime policy

The persisted utility artifact is now connected to `DecisionEngine` behind an
opt-in safety gate. A promoted, schema-compatible, sufficiently confident model
may select Sequential, ThreadPool, or oneTBB; otherwise the existing analytical
and historical provider remains authoritative. The runtime decision report
records load, compatibility, promotion, confidence, application, and fallback
reason diagnostics.


## V1 Phase 2 — OpenCV Test 1

A first OpenCV integration benchmark has been added. The binary-threshold test
uses identical pixel work for OpenCV `parallel_for_` and SmartParallel, checks
all outputs against `cv::threshold`, records the SmartParallel-selected plan,
and emits median timing results for small through 4K images.

Run: `run_opencv_benchmarks.bat`

Output: `validation/output/opencv_test1_threshold.csv`
## Optimization update: cached sequential fast path

`parallel_for` now bypasses workload analysis and decision ranking when a reused callback profile already predicts that parallel execution cannot meet the configured minimum speedup. The optimization is based on measured callback cost rather than a raw iteration threshold, so expensive small ranges remain eligible for parallel execution. Diagnostics expose `sequential_fast_path`, and the behavior is configurable through `enable_parallel_for_cached_sequential_fast_path`.


## Optimization update: non-sticky sequential cache

The cached sequential fast path now requires multiple independently sampled observations, a confidence margin below break-even, and periodic regional revalidation. Cache hits do not increase confidence. A fresh observation that contradicts the cached sequential/parallel classification replaces it immediately, preventing permanent sequential lock-in when callback cost changes.
