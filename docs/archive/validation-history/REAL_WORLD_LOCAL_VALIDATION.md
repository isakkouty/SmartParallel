# Real-World Optimization Local Validation

Validation date: 2026-07-22

## Environment

- Compiler: GCC 14.2.0
- Secondary compiler: Clang 17
- Build type: Release
- Platform: Linux validation container
- Worker limit: 4
- LZ4: available
- OpenCV C++ development package: unavailable locally
- oneTBB: unavailable locally
- MSVC: unavailable locally

The unchanged Windows build requires real oneTBB and installs OpenCV/LZ4 through
vcpkg, so MSVC/OpenCV/oneTBB remain explicit Windows-side gates.

## Build and test results

- SmartParallel full available Release build: **PASS**
- Core-only dependency-free configuration: **PASS**
- LZ4 integration: **PASS**
- BVH integration: **PASS**
- Particle integration: **PASS**
- Release CTest matrix: **18/18 PASS**
- Clang 17 focused optimization/frontier tests: **PASS**
- ASan + UBSan focused optimization/frontier tests: **PASS**
- Existing nested benchmark correctness: **PASS**
- Maximum configured root concurrency: **4**

ThreadSanitizer could not link in the installed Swift/Clang toolchain because its
runtime references unavailable libdispatch/Blocks symbols. No project test ran
under that failed toolchain, so no TSan result is claimed.

## Representative local particle results

Seven repetitions, ThreadPool available, no trace. Values are development
observations rather than Windows performance claims.

| Preset | Sequential | Smart auto | Outer-only TP | Flattened TP |
|---|---:|---:|---:|---:|
| Uniform | 43.218 ms | **28.208 ms** | 31.157 ms | 29.985 ms |
| Sparse | 27.748 ms | 20.664 ms | 19.111 ms | **15.800 ms** |
| Sudden count change | 63.908 ms | 29.511 ms | 32.273 ms | **27.559 ms** |
| Gradual increase | 38.846 ms | 22.335 ms | 21.171 ms | **20.044 ms** |
| Moving clusters | 92.006 ms | **38.104 ms** | 38.977 ms | 38.600 ms |

The sealed descendant path removed most of the previously accumulated nested
entry cost locally. Sparse remains the clearest case where explicit flattening
retains an advantage.

## Other representative results

- LZ4 large blocks: 22.995 ms sequential, **8.048 ms automatic**.
- LZ4 mixed sizes: 66.908 ms sequential, 20.989 ms automatic, 19.671 ms manual TP.
- BVH small uniform: 0.109 ms sequential, 0.118 ms automatic.
- BVH highly unbalanced: 8.594 ms sequential, 4.891 ms automatic, 3.616 ms manual TP.
- Nested four-level automatic remained correct and about 14% behind manual L3
  in the local seven-repetition check.

## Trace and CPU metric check

The updated trace exported separate in-flight drain, actual blocking wait,
signal-to-wake, and completion epilogue fields. The sample trace kept
signal-to-wake at zero unless a real wait occurred.

CPU metrics distinguished approximately one equivalent core for sequential work
from roughly three-to-four equivalent cores for parallel work.

## Platform gate

Run on Windows:

```bat
set VCPKG_ROOT=D:\Tools\vcpkg
scripts\benchmarks\run_real_world_complete.bat 31
```

Upload `validation\output\real_world` for the authoritative OpenCV, real-oneTBB,
and MSVC comparison.
