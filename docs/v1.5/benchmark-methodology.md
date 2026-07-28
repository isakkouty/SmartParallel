# v1.5 benchmark methodology

The v1.5 publication benchmark is designed to answer four separate questions without conflating them:

1. Does every implementation produce exactly the same output?
2. Is SmartParallel's Native threshold kernel competitive with a simple independent compiler loop?
3. Does Auto settle on a route close to the fastest eligible route under current conditions?
4. Does the stable automatic decision add material overhead?

## Operation and presets

The tested operation is exact one-channel `uint8_t` binary thresholding:

```text
destination = source > threshold ? maximum : 0
```

The publication matrix includes contiguous 320×240, 640×480, 1920×1080, 3840×2160, and 7680×4320 images, plus a 1920×1080 strided ROI case.

## Implementations

Each preset is measured through:

- `direct_sequential` — an independent compiler-generated reference loop;
- `opencv_api` — direct `cv::threshold`;
- `smart_auto` — the public automatic semantic API;
- forced Native Sequential;
- forced Native ThreadPool;
- forced Native StaticThread;
- forced Native oneTBB;
- forced SmartParallel OpenCV.

StaticThread remains a diagnostic route but is not an automatic threshold candidate in v1.5.

## Identical memory conditions

Every implementation uses the same source allocation and the same destination allocation within a preset. The destination is reset outside the timed region. Raw evidence records exact addresses, strides, and alignment classes, and the analyzer rejects a matrix if those conditions differ between routes.

This prevents one implementation from receiving a more favorable address, page layout, or alignment than another.

## Correctness and authentication

Every measured result is compared byte-for-byte with the expected output, including ROI row padding. Raw rows include an exact mismatch count and checksum. Forced routes must authenticate the requested backend; Auto must authenticate the selected route.

Any mismatch or authentication failure invalidates the publication run before performance is analyzed.

## Measurement phases

### Cold call

One cold Auto call is recorded for completeness. It is not mixed into steady-state medians.

### Initial learning

Each eligible route is primed twice without ranking those calls. Measured observations then use balanced route orders. The selector begins with three samples per active route and extends ambiguous comparisons up to the bounded sample window. Median and MAD estimates support elimination of clearly slower routes.

A provisional winner must pass an independent holdout against the best remaining competitor before becoming stable.

### Deployment settling

The workload then changes from repeated Auto calls to a balanced interleaved route regime. Production drift sentinels and current-context ABBA revalidation remain enabled. If the original route is no longer competitive, SmartParallel can switch before measurement.

The benchmark requires a bounded clean streak after the final route change. This phase proves adaptation rather than merely training directly under the final measurement pattern.

### Frozen steady-state matrix

Only after adaptation settles does the benchmark pause route maintenance and record the requested odd number of steady-state samples. Pausing maintenance avoids counting deliberate probes as ordinary Auto calls; production maintenance remains enabled outside this publication window and is covered by deterministic tests.

Route order uses a balanced Williams-style design rather than a simple cyclic rotation, reducing position and predecessor bias.

### Dispatch-overhead matrix

A separate adjacent batched comparison measures Auto against the equivalent forced selected route:

```text
Auto → Forced → Forced → Auto
Forced → Auto → Auto → Forced
```

Each block runs long enough to reduce clock and runtime noise. The analyzer reports a point estimate and robust 95% interval.

## Proof gates

### Route selection

The forced implementation corresponding to Auto's settled route must be within:

```text
5% or 1 µs
```

of the fastest eligible forced route. This evaluates route quality independently from Auto lookup cost.

### Native kernel

Native Sequential must be within:

```text
10% or 0.5 µs
```

of the independent compiler-generated loop. This prevents Native performance regressions from being hidden by changes to the reference implementation.

### Stable dispatch

The stable dispatch gate fails only when the lower robust 95% confidence bound exceeds 1 µs. Wide intervals are reported as inconclusive passes rather than interpreted as precise overhead estimates.

### Combined gate

A preset passes only when correctness, authentication, route selection, native-kernel quality, and dispatch requirements all pass.

## Statistical summaries

Steady-state runtime uses the observed median from an odd number of samples. Variability uses median absolute deviation. The benchmark reports raw durations and absolute tolerances because percentages alone are misleading for operations that complete in only a few microseconds.

## Scope of claims

The accepted results apply to the recorded machine, compiler, OpenCV build, worker budget, image layouts, and threshold operation. They do not establish that SmartParallel, OpenCV, ThreadPool, or oneTBB is universally fastest.

The supported release claim is narrower and stronger:

> For this operation and recorded machine, SmartParallel Auto produced correct output, adapted to a changed performance regime, and settled within the declared equivalence gate for all six presets.
