# SmartParallel v1.8 — Release confidence and readiness

This report separates objective pass gates from statistical performance claims. It does not average unrelated evidence into a fake probability.

## Release-readiness rubric

| Category | Points | Evidence |
|---|---:|---|
| Correctness and regression | 25/25 | GCC complete suite: 26/26 passed |
| Concurrency and synchronization | 20/20 | Budget, cancellation, fairness, nested, and multi-Runtime gates passed |
| Deterministic behavior | 10/10 | Exact grant succeeds; insufficient budget fails before output modification |
| Benchmark quality | 15/15 | Alternating order, warmups, raw samples, corrected units, retained outliers, 95% paired bootstrap intervals |
| Cross-platform validation | 6/10 | Linux GCC and Clang validated; final Windows/MSVC publication pending |
| Sanitizers and robustness | 10/10 | ASan/UBSan 2/2 and TSan 2/2 focused v1.8 suites passed |
| Documentation accuracy | 5/5 | Linux-only evidence is labeled correctly; no placeholder Windows result |
| Packaging and reproducibility | 5/5 | Deterministic source-only archive, manifest verification, duplicate/unsafe-entry checks |
| **Current score** | **96/100** | **Conditional release-candidate readiness** |

## Mandatory blocker

The score does not override mandatory platform gates. Final v1.8 acceptance remains blocked until the Windows/MSVC `31 full` workflow succeeds and its evidence is reviewed.

## Performance evidence

The Linux workload demonstrated governance correctness but not a performance win:

- governed/ungoverned throughput ratio: `0.771707 [0.677117, 0.850243]`;
- governed/ungoverned p95-latency ratio: `1.29830 [1.17206, 1.44233]`;
- governed/ungoverned completion-balance ratio: `1.69773 [1.46891, 1.73482]`.

These results remain `FAIL` performance comparisons. They do not invalidate the mandatory resource-safety gates.

## Readiness decision

The implementation, Linux evidence, sanitizers, documentation, and packaging are release-candidate quality. Final release acceptance is conditional on the clean Windows/MSVC publication run and a regenerated cross-platform comparison based on real Windows raw data.
