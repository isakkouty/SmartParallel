# SmartParallel v1.3

> **Current release:** v1.3.0

SmartParallel v1.3 is the cross-platform CI and portability release. It retains the stabilized v1.1 scheduler, nested-execution semantics, public API, and benchmark algorithms while adding automated build, test, install, and package-consumer validation across Windows, Linux, and macOS.

## Release focus

- Windows with MSVC and oneTBB.
- Linux with GCC and Clang.
- macOS with Apple Clang.
- oneTBB-enabled and oneTBB-disabled configurations.
- Debug and Release correctness coverage.
- AddressSanitizer and UndefinedBehaviorSanitizer validation on Linux.
- Installation and external `find_package(SmartParallel CONFIG)` consumption.
- Native hardware discovery on Windows, Linux, and macOS for scheduler topology inputs.
- Persistent vcpkg binary caching for GitHub Actions.

The real-world performance benchmarks are intentionally not executed in CI. Their final recorded v1.1 results remain the performance reference because v1.3 does not change scheduler or benchmark behavior.

## Final validation status

The final v1.3 pull-request workflow passed all six supported configurations:

| Job | Result |
|---|---:|
| Windows/MSVC Release, oneTBB required | Passed |
| Linux/GCC Debug, oneTBB disabled | Passed |
| Linux/GCC Release, oneTBB required | Passed |
| Linux/Clang Release, oneTBB required | Passed |
| macOS/Apple Clang Release, oneTBB required | Passed |
| Linux/Clang Debug, ASan + UBSan, oneTBB disabled | Passed |

Each normal operating-system job passed all 16 deterministic tests, installed the library, and built and ran the separate `find_package` consumer. The sanitizer job passed the same 16 deterministic tests. No real-world performance benchmark was run in CI.

The cross-platform nested-frontier test verifies the stable invariants rather than forcing one hardware-dependent frontier depth: levels 1 and 2 are deferred, a bounded frontier appears at level 3 or 4, level-4 descendants are suppressed when level 3 is selected, every leaf executes exactly once, and the four-worker lease budget is respected.

## Documentation

- [CI and GitHub setup](ci-and-github-setup.md)
- [Portability audit](portability-audit.md)
- [Native hardware discovery](native-hardware-discovery.md)
- [Cross-platform build and installation](installation.md)
- [v1.3 release notes](release-notes.md)
- [Local and GitHub Actions validation](local-validation.md)
- [Release checklist](release-checklist.md)
- [v1.1 runtime and scheduler documentation](../v1.1/README.md)
- [Final real-world benchmark report](../v1.1/benchmarks.md)

## Quick local validation

Without oneTBB:

```text
cmake --preset ci-debug-no-tbb
cmake --build --preset ci-debug-no-tbb
ctest --preset ci-debug-no-tbb
```

With oneTBB through vcpkg:

```text
set VCPKG_ROOT=D:\Tools\vcpkg
cmake --preset ci-release-tbb
cmake --build --preset ci-release-tbb
ctest --preset ci-release-tbb
```

On Linux or macOS, set `VCPKG_ROOT` to the corresponding vcpkg checkout before using the oneTBB preset.
