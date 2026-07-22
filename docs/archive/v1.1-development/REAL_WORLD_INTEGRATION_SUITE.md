# SmartParallel v1.1 Real-World Integration and Optimization Suite

This source package contains four optional, deterministic real-world
integrations: OpenCV image processing, LZ4 batch compression, recursive BVH
construction, and a repeated particle simulation.

The optimization update preserves the existing project philosophy:

- root CMake options,
- vcpkg manifest dependencies,
- Visual Studio 2022/NMake batch scripts,
- existing backend names and authenticity tracing,
- separate real-world CSV schema without changing nested-validation CSVs,
- CTest smoke and optimization-hardening coverage,
- no benchmark dependency in the installed core library.

The focused additions are:

- frontier-sealed descendant direct execution,
- bounded per-root resolved-plan reuse,
- exactly-once analytical cold-root learning,
- bounded benchmark backend calibration,
- weighted largest-first OpenCV work decomposition,
- causal helper-wait trace fields,
- corrected process CPU metrics.

## Primary Windows command

```bat
set VCPKG_ROOT=D:\Tools\vcpkg
scripts\benchmarks\run_real_world_complete.bat 31
```

The command configures with the existing NMake/MSVC path, installs missing
manifest dependencies, builds all integrations, runs CTest, executes every
preset/mode/backend, writes traces, performs comparison, and stops on the first
failure.

Detailed instructions:

- `REAL_WORLD_OPTIMIZATION_UPDATE.md`
- `benchmarks/v1.1.0/real_world/README.md`
- `benchmarks/v1.1.0/real_world/CSV_SCHEMA.md`
