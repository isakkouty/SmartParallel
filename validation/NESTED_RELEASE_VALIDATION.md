# SmartParallel v1.1 final nested release validation

Validation was performed on 2026-07-21 with the ThreadPool release gate and a four-participant root-session budget. Real oneTBB runtime validation is intentionally left as a required platform-specific command because oneTBB was unavailable in the local container.

## Correctness and concurrency gates

- GCC 14.2 Release CTest: **12/12 passed**.
- Clang 17 Release targeted nested CTest: **7/7 passed**.
- AddressSanitizer + UndefinedBehaviorSanitizer targeted nested CTest: **7/7 passed**.
- ThreadSanitizer targeted nested CTest: **7/7 passed**.
- Exactly-once, deep nesting, irregular trees, nested exceptions, cancellation cleanup, reentrant roots, concurrent roots, and root lease bounds passed.
- Long-running cache/trace retention, stable-plan revalidation, StaticThread session participation, and near-limit chunk arithmetic passed.

## Local regular-tree performance

31 measured Release repetitions, shape `2 x 3 x 4 x 192`:

| Configuration | Median |
|---|---:|
| Sequential | 6.95 ms |
| Level 3 only | 2.59 ms |
| Forced all levels | 2.71 ms |
| Automatic all levels | 2.90 ms |
| Flattened N-D | 2.03 ms |

Automatic execution was approximately 12% slower than the best manually selected frontier and selected the intended L3 frontier.

Three traced repetitions recorded:

- maximum root-session leases: **4**;
- maximum helper work-drain interval: **0.972 ms**;
- maximum completion-signal-to-wake interval: **0.166 ms**;
- maximum helper wait count per region: **1**;
- no 14–16 ms completion mode.

## Windows release commands

```bat
scripts\validation\run_nested_release_validation.bat 31
scripts\validation\run_nested_release_validation.bat 3 trace
scripts\validation\run_nested_release_validation.bat 11 tbb
scripts\validation\run_nested_release_validation.bat 3 trace tbb
```

Normal, trace, TBB, and TBB-trace runs use separate output filenames.

## Files to return for review

```text
validation\output\v1.1.0_nested_execution_optimized.csv
validation\output\v1.1.0_nested_execution_optimized_raw.csv
validation\output\v1.1.0_nested_execution_optimized_trace_run.csv
validation\output\v1.1.0_nested_execution_optimized_trace_run_raw.csv
validation\output\v1.1.0_nested_execution_optimized_trace_run_trace.csv
validation\output\v1.1.0_nested_execution_optimized_tbb_run.csv
validation\output\v1.1.0_nested_execution_optimized_tbb_run_raw.csv
```
