# SmartParallel v1.1 High-Confidence Finishing Update

This update applies the final five engineering changes selected after the isolated
real-world run. It deliberately avoids selective descendant borrowing or dynamic
frontier migration. The existing public execution model, CMake options, vcpkg
manifest, NMake/MSVC workflow, and benchmark command names are unchanged.

## 1. Warm-up-only backend calibration

The bounded ThreadPool/oneTBB calibration cache now has explicit mutable and
frozen phases.

- Cold and warm-up executions may collect calibration observations.
- After the configured warm-ups, the real-world harness freezes the cache.
- Timed repetitions and the representative diagnostic execution reuse the
  resolved winner.
- A frozen cache never probes an alternative backend and never updates its
  statistics.
- If a workload bucket did not resolve a winner during warm-up, frozen execution
  uses the scheduler's requested backend rather than exploring during timing.

This makes measured repetitions deterministic with respect to backend
calibration while retaining bounded warm-up learning.

## 2. Exactly-once pilot cold start for coarse recursive roots

A cold root that has enough items for the worker budget but fewer items than the
normal analytical threshold can use one in-band pilot item.

- The pilot callback executes exactly once as real useful work.
- Its elapsed time estimates the total sequential cost.
- Only the unexecuted remainder is promoted when that estimate exceeds the
  configured threshold.
- The pilot runs under conservative descendant learning.
- Promoted remainder work seals descendants beneath the selected frontier.

This targets four-child recursive roots such as BVH construction without
parallelizing every cheap four-item loop and without speculative callback
sampling.

## 3. Stable tiny-work absolute-cost bypass

A repeatedly observed automatic root whose inclusive estimated work is below a
strict absolute threshold now executes directly and seals descendants
sequentially.

The bypass requires:

- automatic backend mode,
- a stable cached profile,
- the configured minimum number of independent observations,
- a positive estimated total cost below the absolute threshold.

It remains subject to periodic profile revalidation. Explicitly forced backends
are not redirected by this rule.

## 4. Batched CPU-time measurement

Process CPU time is accumulated across the full timed repetition batch before
computing equivalent cores. This avoids treating a single Windows process-time
tick as a precise sub-millisecond measurement.

New reporting behavior:

- substantial timed batches report normalized utilization and equivalent cores,
- unavailable or physically inconsistent short-batch measurements are marked
  unavailable instead of reporting impossible values,
- raw rows include CPU seconds, availability, and measurement scope,
- summary rows include total timed-batch CPU seconds and wall time.

## 5. Invariant and complete result reporting

The PowerShell comparator now runs under invariant culture, so generated
combined CSV numeric fields always use decimal points. The environment output
records the actual real-world calibration policy, and report generation fails if
the Markdown analysis file is missing or empty.

## Configuration additions

The core defaults add these bounded controls:

- `enable_root_pilot_cold_start`
- `root_pilot_cold_min_estimated_work_ms`
- `enable_parallel_for_tiny_work_bypass`
- `parallel_for_tiny_work_bypass_max_ms`
- `parallel_for_tiny_work_bypass_min_observations`

The core backend calibration feature remains disabled by default. The
real-world benchmark suite enables it and freezes it after warm-up.

## Validation

Local validation completed on the final source tree:

- GCC 14.2 Release configuration and build
- 15/15 available CTests passed
- Clang 17 focused hardening build and test passed
- AddressSanitizer + UndefinedBehaviorSanitizer focused test passed
- LZ4, BVH, and particle real-world targets compiled
- targeted BVH and particle runs passed checksums and backend confirmation
- OpenCV and real oneTBB remain Windows/vcpkg validation items

## Complete Windows command

From the extracted project root:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\benchmarks\run_real_world_complete.bat 31
```
