# v1.5 vision API

The v1.5 semantic API is provided by the optional `SmartParallel::vision` target. It does not change or replace the existing scheduler and v1.4 algorithm APIs.

## Build and link

Enable the module without OpenCV:

```text
cmake -S . -B build/vision -DSMARTPARALLEL_BUILD_VISION=ON
```

Enable the optional OpenCV route:

```text
cmake -S . -B build/vision-opencv \
  -DSMARTPARALLEL_BUILD_VISION=ON \
  -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=ON
```

Installed consumers use the separate package:

```cmake
find_package(SmartParallelVision CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE SmartParallel::vision)
```

The core-only package remains:

```cmake
find_package(SmartParallel CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE SmartParallel::smart_parallel)
```

## Image views

```cpp
smart::vision::ImageView<const std::uint8_t> source;
smart::vision::ImageView<std::uint8_t> destination;
```

Views are non-owning. They contain data, width, height, row stride, and channel count. The first semantic operation supports one-channel `uint8_t` images, contiguous buffers, strided rows, and exact in-place execution.

Helpers:

```cpp
smart::vision::make_contiguous_image_view(data, width, height);
smart::vision::make_image_view(data, width, height, stride_bytes);
```

The caller owns the memory and must keep it valid for the duration of the call. Partial source/destination overlap is rejected; exact in-place use is supported.

## Threshold

```cpp
smart::vision::threshold(
    source,
    destination,
    smart::vision::ThresholdOptions{
        127,
        255,
        smart::vision::ThresholdMode::Binary
    });
```

The exact contract is:

```text
Binary:        destination = source > threshold ? maximum : 0
BinaryInverse: destination = source > threshold ? 0 : maximum
```

Automatic route selection is the default. The call remains the same whether the module was built Native-only or with the optional OpenCV provider.

A complete example is available at [`examples/v15_adaptive_threshold.cpp`](../../examples/v15_adaptive_threshold.cpp).

## Diagnostic forcing

```cpp
smart::vision::ExecutionPolicy policy;
policy.route = smart::vision::ExecutionRoute::OpenCV;
policy.worker_budget = 4;
```

Forceable routes are:

- `Auto`;
- `NativeSequential`;
- `NativeThreadPool`;
- `NativeStaticThread`;
- `NativeOneTbb`;
- `OpenCV`.

Forcing is intended for validation, benchmarking, reproducibility, and troubleshooting. A forced unavailable route fails clearly rather than silently authenticating a different route.

## Decision diagnostics

```cpp
const auto report = smart::vision::last_decision_report();
```

The report includes:

- requested and selected routes;
- cache, learned-route, and adaptive-selection state;
- exploration, holdout, drift, and revalidation probe state;
- worker budget, participants, chunks, and execution depth;
- OpenCV availability;
- whether internal timing was active;
- backend-authentication status.

Stable thread-local hot-route calls intentionally skip internal timing except for sparse drift sentinels. Publication benchmarks time the complete public API externally.

## Training and adaptation diagnostics

```cpp
const auto training = smart::vision::last_route_training_report();
```

The report exposes:

- training median, MAD, minimum, and maximum per route;
- measured, priming, holdout, and current sample counts;
- provisional and stable routes;
- training and current baselines;
- drift strikes and route-switch count;
- latest stable/challenger current-context comparison.

## Provider and cache invalidation

```cpp
smart::vision::refresh_provider_state();
smart::vision::clear_adaptive_route_cache();
```

Call `refresh_provider_state()` after changing OpenCV process-global settings such as thread count, optimization state, or OpenCL configuration. It refreshes the provider fingerprint and invalidates incompatible learned routes.

`clear_adaptive_route_cache()` clears process-wide operation learning and invalidates current-thread hot decisions. It is primarily useful for deterministic tests and controlled benchmark learning phases.
