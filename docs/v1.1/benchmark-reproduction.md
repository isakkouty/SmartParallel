# Benchmark reproduction

> **Current documentation:** SmartParallel v1.1.0.

## Complete Windows run

From the repository root, with Visual Studio 2022 C++ tools and vcpkg installed:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\benchmarks\run_real_world_complete.bat 31
```

Replace `D:\Tools\vcpkg` with the actual vcpkg directory. The script configures a clean Release NMake build, resolves manifest dependencies, runs CTest, executes every real-world integration/mode/preset, validates correctness, compares results, and writes output under:

```text
validation/output/real_world/
```

## Development run

```bat
scripts\benchmarks\run_real_world_development.bat 15
```

## One integration

```bat
scripts\benchmarks\run_real_world_integration.bat particles 31 all all all trace
```

Replace `particles` with `opencv`, `lz4`, or `bvh` as needed.

## Regenerate release figures

Install the plotting dependencies:

```text
python -m pip install pandas matplotlib
```

Then run from the repository root:

```text
python tools/plot_real_world_results.py
```

The script reads `validation/output/real_world`, validates correctness/backend/budget fields, and writes PNG/SVG figures plus generated metrics to `docs/v1.1/assets/benchmarks/`.

Custom paths are supported:

```text
python tools/plot_real_world_results.py \
  --input-dir path/to/real_world \
  --output-dir path/to/figures
```
