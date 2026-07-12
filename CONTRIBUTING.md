# Contributing

SmartParallel is a Beta 1.0 project. Contributions are welcome, but changes should preserve the central design principle: user code describes the algorithm while the framework chooses the execution policy.

## Before changing code

1. Build the project in Release mode.
2. Run the relevant benchmark or test before the change.
3. Keep public API changes small and explain why they are necessary.
4. Do not describe planned behavior as implemented behavior.

## Build

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

## Style

- C++17
- four-space indentation
- descriptive names over abbreviations
- headers grouped by subsystem under `include/smart/`
- templates in headers; non-template implementation in `src/`
- avoid shared mutable state in callbacks
- comments should explain intent or constraints, not restate syntax

## Benchmark changes

A performance change should include:

- the benchmark and input sizes used
- compiler and build mode
- hardware thread count
- before/after CSV data
- an explanation of noise control
- disclosure of regressions, not just improvements

Run all plot generation with:

```powershell
py benchmarks\plot_all.py
```

Do not commit generated executables or Python cache directories.

## Pull-request checklist

- [ ] Release build succeeds
- [ ] Relevant tests and benchmarks pass
- [ ] No new compiler warnings
- [ ] Documentation reflects actual behavior
- [ ] CSV schema changes are reflected in plotting code
- [ ] Public API changes are documented
