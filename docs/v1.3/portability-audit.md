# Portability audit

This audit records the operating-system assumptions reviewed for SmartParallel v1.3.

## Build and package system

- CMake requires version 3.20 or newer and C++17.
- The exported target remains `SmartParallel::smart_parallel`.
- The library links and exports CMake's portable `Threads::Threads` dependency.
- oneTBB remains optional. `SMARTPARALLEL_ENABLE_TBB=OFF` compiles the sequential, ThreadPool, and StaticThread paths without finding or linking TBB.
- `SMARTPARALLEL_REQUIRE_TBB=ON` makes missing oneTBB a configuration error in CI jobs that promise oneTBB coverage.
- The installed package conditionally finds TBB only when SmartParallel was built with TBB support.
- A separate consumer validates installation, package discovery, public headers, transitive dependencies, native hardware discovery, and execution.

## Runtime portability

The scheduling, synchronization, cancellation, and nested-session layers use standard C++17 threads, mutexes, condition variables, atomics, futures, containers, and `std::chrono::steady_clock`. They do not use platform affinity APIs or platform-specific filesystem APIs.

Hardware discovery is isolated in `src/hardware.cpp` and has native guarded implementations:

### Windows

- Logical processors: `GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)` with a standard-library fallback.
- Physical cores, cache topology, and NUMA nodes: `GetLogicalProcessorInformationEx`.
- Page size: `GetSystemInfo`.
- Existing Windows cache aggregation and scheduler inputs are preserved.

### Linux

- Logical processors: the current process affinity mask from `sched_getaffinity`, then `_SC_NPROCESSORS_ONLN`, then `std::thread::hardware_concurrency`.
- Physical cores: unique package/die/core tuples from `/sys/devices/system/cpu/cpu*/topology` for CPUs available to the process.
- Caches and cache-line size: unique cache instances from `/sys/devices/system/cpu/cpu*/cache` with shared-cache deduplication.
- NUMA nodes: `/sys/devices/system/node/online`, with directory enumeration as a fallback.
- Page size: `_SC_PAGESIZE`.
- Missing, container-restricted, or virtualized files retain conservative defaults and unavailable flags instead of causing failure.

### macOS

- Logical and physical processors: `hw.logicalcpu` and `hw.physicalcpu` through `sysctl`.
- Page size: `hw.pagesize`, then `_SC_PAGESIZE`.
- Cache-line and cache sizes: `hw.cachelinesize`, `hw.l1dcachesize`, `hw.l1icachesize`, `hw.l2cachesize`, and `hw.l3cachesize` when exposed.
- macOS has no stable public NUMA-topology API. SmartParallel therefore retains one NUMA node with `numa_info_available=false` rather than guessing from packages or heterogeneous performance levels.

### Other C++17 platforms

- Logical and physical counts use the standard/POSIX online-processor fallback.
- Page size is queried through `sysconf` where available.
- Cache and NUMA values retain conservative defaults.

Native Linux and macOS discovery is cached once per process. This avoids filesystem or `sysctl` work in scheduling hot paths while preserving the same immutable hardware inputs for the lifetime of the process.

## Diagnostics and manual benchmark metadata

- UTC conversion uses `gmtime_s` on Windows and `gmtime_r` on Unix-like systems.
- Process CPU time uses `GetProcessTimes` on Windows and `getrusage` on Linux/macOS.
- Peak resident memory uses `GetProcessMemoryInfo` on Windows and the platform-correct `ru_maxrss` units on Linux/macOS.
- Linux processor names use `/proc/cpuinfo`, DMI, and `uname` fallbacks.
- macOS processor names use `machdep.cpu.brand_string`, `hw.model`, and `uname` fallbacks.
- Windows oneTBB DLL copying remains guarded by `WIN32`; Unix platforms use their normal loader/package paths.

## Tests and CI

- The CI workflow builds deterministic correctness and hardening tests only; real-world performance benchmarks remain disabled.
- The final v1.3 pull-request workflow passed all six configured jobs across Windows/MSVC, Linux/GCC, Linux/Clang, macOS/Apple Clang, oneTBB enabled/disabled, and Clang ASan+UBSan.
- `smartparallel_hardware_characteristics_portability` validates nonzero and internally consistent topology values on every CI operating system.
- The external installed-package consumer calls `hardware_characteristics()` as well as `smart::parallel_for`, validating that the out-of-tree package includes the platform implementation.
- Timeout guards protect against deadlocks and are not performance thresholds.
- Linux Clang runs AddressSanitizer and UndefinedBehaviorSanitizer without oneTBB.

## Behavior boundaries

- Hardware values are advisory scheduler inputs, not correctness assumptions.
- A missing OS facility cannot disable execution; SmartParallel falls back conservatively.
- No scheduler rule, backend algorithm, nested-budget semantic, benchmark workload, or public execution contract was changed for this portability work.
- Performance equivalence across operating systems still requires manual physical-machine benchmarks; CI intentionally verifies correctness rather than speed.

## Platform-independent nested-frontier validation

The nested-frontier validation removes test-only profitability barriers and validates scheduler invariants rather than one exact frontier depth. Different compiler, timer, and hardware-model combinations can legitimately establish the bounded frontier at level 3 or at the level-4 leaf while preserving the same public semantics.

The test requires levels 1 and 2 to be deferred, a bounded frontier at level 3 or 4, descendant suppression when level 3 is selected, exactly-once leaf execution, and no more than four root worker leases. Production configuration defaults, decision rules, and scheduler behavior are unchanged.

