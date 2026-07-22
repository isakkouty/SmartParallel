# Local portability validation

This page records validation performed while preparing SmartParallel v1.3.0. It complements, but does not replace, the GitHub Actions matrix on the target operating systems.

## Completed locally

| Toolchain/configuration | Result |
|---|---|
| GCC 14.2, Linux, Release, oneTBB disabled | library and all 16 deterministic CTest tests passed |
| GCC 14.2, native Linux hardware probe | affinity-aware logical/physical topology, page size, cache line, L1/L2/L3, and NUMA discovery passed |
| Clang 17, Linux, Release, oneTBB disabled | all 16 deterministic CTest tests passed |
| Clang 17, Linux, Debug, ASan + UBSan, oneTBB disabled | all 16 deterministic CTest tests passed |
| Cross-compiler frontier mechanics regression | bounded L3-or-L4 frontier contract passed under GCC Release, Clang Release, and Clang ASan/UBSan |
| Clang-installed package consumed by a separate GCC project | installation succeeded; external `find_package` consumer built and passed, including `hardware_characteristics()` |
| GCC 14.2, custom BVH and particle real-world targets | compiled successfully after the cross-platform metadata changes; performance runs were not executed |
| oneTBB required configuration with TBB unavailable | configuration failed immediately as intended |
| documentation and JSON validation | passed |

The Linux probe in the preparation environment reported five affinity-available logical processors, five physical cores, a 4096-byte page, a 64-byte cache line, detected L1/L2/L3 cache topology, and one NUMA node. These values describe the validation container, not a performance reference machine.

The external consumer verifies the installed target name, public headers, version metadata, transitive `Threads::Threads` dependency, native hardware implementation, and a correctness-checked `smart::parallel_for` execution.

## GitHub Actions target-platform validation

The final v1.3 pull-request workflow passed all supported jobs:

| GitHub Actions job | Result | Coverage |
|---|---:|---|
| `windows-msvc-release-tbb` | Passed | MSVC Release, oneTBB required, 16/16 tests, install, external consumer |
| `linux-gcc-debug-no-tbb` | Passed | GCC Debug, oneTBB disabled, 16/16 tests, install, external consumer |
| `linux-gcc-release-tbb` | Passed | GCC Release, oneTBB required, 16/16 tests, install, external consumer |
| `linux-clang-release-tbb` | Passed | Clang Release, oneTBB required, 16/16 tests, install, external consumer |
| `macos-appleclang-release-tbb` | Passed | Apple Clang Release, oneTBB required, 16/16 tests, native `sysctl` probe, install, external consumer |
| `linux-clang-debug-asan-ubsan` | Passed | Clang Debug, oneTBB disabled, ASan + UBSan, 16/16 tests |

The macOS job initially exposed an overly specific test expectation. A valid automatic policy could establish the bounded nested frontier at level 4 instead of level 3 on Apple Clang. The final deterministic test therefore validates the actual cross-platform contract:

- levels 1 and 2 are deferred because they underfill the four-worker root budget;
- a bounded frontier is established at level 3 or level 4;
- when level 3 is selected, level-4 descendants use the frontier fast path;
- every leaf executes exactly once;
- the root session never exceeds four leased workers.

This is a test-only portability correction. It does not change scheduler decisions, production thresholds, runtime learning, backend execution, or benchmark behavior.

Real-world performance benchmarks remain intentionally excluded from CI. Cross-platform performance equivalence must be measured manually on controlled physical machines.
