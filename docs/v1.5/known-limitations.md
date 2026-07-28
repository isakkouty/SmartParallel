# v1.5 known limitations

## One semantic operation

v1.5 begins with exact one-channel 8-bit binary and inverse-binary thresholding. It is an architectural vertical slice, not an OpenCV wrapper collection.

## Opaque lambdas remain Native-only

SmartParallel cannot safely prove that an arbitrary compiled `parallel_transform` callable is equivalent to `cv::threshold` or another specialized function. Existing generic algorithms continue to select only Native scheduling strategies. Specialized substitution requires a semantic operation such as `smart::vision::threshold`.

## Learning is in-process

Route decisions are not persisted in v1.5. A new process repeats the bounded learning phase. Versioned persistent operation profiles remain a future extension.

## Root-only adaptive provider selection

Nested Auto calls use Native Sequential in the first release. External provider runtimes are not yet integrated into the root participant-lease accounting system.

## OpenCV thread configuration is external

SmartParallel fingerprints but does not mutate `cv::setNumThreads()` per call because it is process-global and unsafe to change concurrently. Applications that change OpenCV's global configuration must call `smart::vision::refresh_provider_state()` afterward.

## Adaptation is bounded, not instantaneous

Stable calls avoid timing overhead except for sparse sentinel observations. A large performance-regime change is normally detected after two sentinel samples and completed over a four-position current-context comparison, so a temporarily suboptimal route can remain active for a bounded number of calls before switching.

## First-use learning has a cost

A profile that has never been seen must prime and measure multiple routes before it can reuse a stable winner. This is appropriate for repeated image processing and pipelines, but a one-off call may not recover its learning cost.

## Small operations expose dispatch floors

For microsecond operations, tens of nanoseconds can appear as a noticeable percentage. Publication analysis reports raw durations, absolute tolerances, and confidence intervals rather than relying only on percentages.

## Optional dependency and binary size

Enabling the provider adds OpenCV core/imgproc deployment dependencies and binary footprint. The core `SmartParallel` package remains independent.

## Machine-specific choices

The fastest route depends on CPU features, memory, compiler, OpenCV build, oneTBB version, worker budget, image layout, and current system state. No checked-in route is universally optimal, and the accepted benchmark route map must not be treated as a fixed tuning table.

## CPU scope

The first Native SIMD implementation provides AVX2 and SSE2 paths for x86-family targets plus a portable branchless scalar fallback. Other architectures remain correct but may not yet have an architecture-specific vector kernel.
