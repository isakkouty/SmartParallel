# OpenCV Benchmarks

This folder contains the SmartParallel OpenCV integration benchmarks.

## Layout

- `src/test1_threshold.cpp` — lightweight threshold/overhead benchmark.
- `src/test2_convolution.cpp` — manual 5x5 convolution.
- `src/test3_sobel.cpp` — manual Sobel gradient magnitude.
- `src/stress_suite.cpp` — six higher-level stress workloads:
  - tiled connected-components analysis,
  - two-pass integral image,
  - tiled Canny edge detection,
  - tiled Harris corner response,
  - tiled Gaussian pyramids,
  - irregular mixed patch processing.
- `scripts/run_suite.bat` — runs every OpenCV benchmark.
- `scripts/run_test1.bat`, `run_test2.bat`, `run_test3.bat` — run one original test.
- `scripts/run_stress.bat` — runs the stress suite.
- `scripts/run_common.bat` — shared configure/build/run implementation.

## Running on Windows

From the repository root:

```bat
run_opencv_benchmarks.bat
```

The scripts use `VCPKG_ROOT` when it is set. A toolchain path may also be passed explicitly:

```bat
run_opencv_benchmarks.bat D:\vcpkg\scripts\buildsystems\vcpkg.cmake
```

CSV results are written to `validation\output`.
