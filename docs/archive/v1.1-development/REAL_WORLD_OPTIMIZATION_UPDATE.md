# SmartParallel v1.1 Real-World Optimization Update

This update implements the focused optimization package derived from the clean
OpenCV, LZ4, BVH, and particle results. It keeps the existing scheduler model,
CMake options, NMake/MSVC workflow, vcpkg manifest, benchmark commands, and CSV
file names.

## Implemented optimizations

### Frontier-sealed descendant direct mode

After an active parallel frontier has made descendants sequential, untraced
warm descendant calls now execute directly. They bypass repeated profile,
callsite, backend, plan, telemetry, and diagnostic construction while preserving
exactly-once execution, exceptions, cancellation state, and explicit forced-mode
semantics. Trace and conservative-learning runs retain the detailed path.

### Session-local resolved-plan memo

A root session can reuse a fully constrained plan for the exact callsite,
iteration count, policy generation, profile generation, runtime domain, and
worker budget. The bounded memo is discarded with the root session and does not
replace process-level revalidation.

### Analytical exactly-once cold root learning

Sufficiently large cold roots may use a conservative analytical plan and learn
from the execution itself. No callback is pre-executed or duplicated. Underfilled
nested roots remain sequential, preserving the validated depth-four frontier.

### Bounded backend calibration

Real-world benchmarks enable a bounded ThreadPool/oneTBB calibration cache for
stable root plans. Switching requires an observed advantage beyond hysteresis.
The core default remains disabled. StaticThread is not automatically explored.

### Weighted OpenCV work decomposition

OpenCV outer work is ordered largest-first by deterministic pixel cost. Images
larger than 1.25 times the per-worker target are split only at the benchmark's
existing tile boundaries. All comparison modes consume the same deterministic
worklist.

### Causal helper timing

The trace now separates:

- in-flight useful-work drain,
- actual blocking wait,
- completion-signal-to-wake time only when a wait occurred,
- completion epilogue.

Existing trace columns remain and the new fields are appended.

### Correct process CPU metrics

The CSV now reports both normalized process CPU utilization and process CPU
equivalent cores. A one-core sequential run should approach one equivalent core;
a saturated four-worker run should approach four.

### Benchmark wrapper race removal

Automatic nested benchmark calls no longer rewrite the process-global backend
configuration. Forced backend scoping is root-only. This removes redundant work
and a benchmark-side concurrent configuration write without changing the
SmartParallel scheduler API.

## Validation target

The new test `smartparallel_real_world_optimization_hardening` verifies:

- exactly-once analytical cold execution,
- the sealed descendant path,
- bounded root plan memoization,
- bounded backend calibration,
- causal completion timing.

Use the unchanged complete command:

```bat
set VCPKG_ROOT=D:\Tools\vcpkg
scripts\benchmarks\run_real_world_complete.bat 31
```
