# SmartParallel v1.3.0 release notes

SmartParallel v1.3 is a portability and continuous-integration release built on the stabilized v1.1 runtime.

## Added

- GitHub Actions coverage for Windows/MSVC, Linux/GCC, Linux/Clang, and macOS/Apple Clang.
- Required oneTBB validation on Windows, Linux Release, Clang Release, and macOS.
- A Debug Linux build with oneTBB explicitly disabled.
- Linux Clang AddressSanitizer and UndefinedBehaviorSanitizer validation.
- Installed CMake package validation through an external consumer project.
- Persistent vcpkg binary caching and a cached fallback vcpkg checkout.
- Cross-platform CI, installation, and portability documentation.
- Reusable CMake presets for no-TBB, TBB-required, and sanitizer validation.

## Fixed

- The installed package now exports its standard thread dependency through `Threads::Threads`, avoiding Unix consumer link failures or toolchain-dependent behavior.
- Linux now reports affinity-aware logical CPUs, physical cores, page size, cache line size, aggregate cache topology, and NUMA-node count through native `/sys` and POSIX facilities.
- macOS now reports logical and physical CPU counts, page size, cache line size, and available cache sizes through `sysctl`. macOS retains a conservative one-node NUMA default because there is no stable public NUMA topology API.
- Hardware discovery retains safe defaults when an operating-system facility is unavailable or restricted.
- Manual real-world benchmark metadata now obtains a useful processor model on Apple and non-x86 Linux systems when the operating system exposes one.
- The deterministic nested-frontier validation now checks a bounded level-3-or-level-4 frontier instead of demanding one exact depth. It still requires outer-level deferral, descendant suppression when level 3 is selected, exactly-once execution, and the four-worker root budget. Runtime thresholds and scheduler policy are unchanged.

## Final validation

The final v1.3 pull-request workflow passed:

- Windows/MSVC Release with oneTBB required;
- Linux/GCC Debug with oneTBB disabled;
- Linux/GCC Release with oneTBB required;
- Linux/Clang Release with oneTBB required;
- macOS/Apple Clang Release with oneTBB required;
- Linux/Clang Debug with AddressSanitizer and UndefinedBehaviorSanitizer.

Each normal platform job passed all 16 deterministic tests, package installation, and the external `find_package` consumer. The sanitizer job passed all 16 tests. Real-world performance benchmarks were not run in CI.

## Unchanged

- Scheduler policy and automatic strategy selection.
- Nested parallelism semantics and worker budgeting.
- Public C++ API.
- Backend algorithms.
- Real-world benchmark algorithms and recorded benchmark data.
- Recorded v1.1 performance results.
- Production nested-frontier thresholds and scheduler decision rules.

Real-world performance benchmarks remain manual and are not merge gates in v1.3 CI.
