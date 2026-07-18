# OpenCV integration — Test 1

This benchmark is the first real-project integration test for SmartParallel.
It applies the same binary-threshold pixel kernel to continuous `cv::Mat`
images using four execution paths:

1. sequential loop;
2. OpenCV `cv::parallel_for_`;
3. SmartParallel `smart::parallel_for`;
4. OpenCV `cv::threshold` as the optimized-library reference.

The fair scheduler comparison is **2 versus 3**, because both execute the same
kernel. `cv::threshold` is included for correctness and to show how far a
generic wrapper remains from a specialized SIMD-optimized OpenCV primitive.

The benchmark covers 64×64, 640×480, 1920×1080, and 3840×2160 images. It uses
median timings after a warm-up and writes:

`validation/output/opencv_test1_threshold.csv`

## Build on Windows with vcpkg

Install OpenCV if needed:

```bat
vcpkg install opencv4:x64-windows
```

Then run from the SmartParallel repository root:

```bat
run_v1_opencv_test1.bat
```

The script uses `%VCPKG_ROOT%` when it is defined. You can also pass an
explicit toolchain file:

```bat
run_v1_opencv_test1.bat D:\vcpkg\scripts\buildsystems\vcpkg.cmake
```

## Interpretation

Success for Test 1 does not require beating `cv::threshold`; that function is a
specialized OpenCV implementation. The initial question is whether
SmartParallel chooses sequential execution for tiny images and a parallel
backend for larger images without losing badly to OpenCV's generic parallel
loop. The CSV records the selected SmartParallel plan for every size.

# OpenCV integration — Test 2

Test 2 uses a substantially heavier **5×5 floating-point convolution** (25
multiply-accumulate operations per output pixel) with replicated borders. It
compares:

1. the same kernel in a sequential row loop;
2. the same kernel through OpenCV `cv::parallel_for_`;
3. the same kernel through SmartParallel `smart::parallel_for`;
4. OpenCV `cv::filter2D` as the specialized-library reference.

The scheduler comparison remains **2 versus 3**. Test 2 is intended to show
whether the extra per-row work is sufficient to amortize SmartParallel's
runtime decision and scheduling overhead.

Run it from the repository root:

```bat
run_v1_opencv_test2.bat
```

It writes:

`validation/output/opencv_test2_convolution.csv`

# OpenCV integration — Test 3

Test 3 evaluates **3×3 Sobel gradient magnitude**. Each output pixel computes
horizontal and vertical Sobel responses and then their Euclidean magnitude. It
compares:

1. the manual kernel in a sequential loop;
2. the same manual kernel through OpenCV `cv::parallel_for_`;
3. the same manual kernel through SmartParallel `smart::parallel_for`;
4. OpenCV `cv::Sobel` plus `cv::magnitude` as the specialized reference.

Run it from the repository root:

```bat
run_v1_opencv_test3.bat
```

It writes:

`validation/output/opencv_test3_sobel.csv`

# Run the full OpenCV suite

```bat
run_v1_opencv_suite.bat
```

The suite runs threshold, convolution, and Sobel benchmarks in order and stops
immediately if configuration, compilation, execution, or correctness fails.
