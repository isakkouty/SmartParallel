# v1.5 architecture

## Two adaptive decisions

SmartParallel v1.4 selects how to schedule one Native SmartParallel implementation. v1.5 adds a higher decision that can select a complete implementation route.

```text
smart::vision::threshold
        ↓
Adaptive execution-route selection
        ├── Native Sequential
        ├── Native ThreadPool
        ├── Native oneTBB
        └── OpenCV threshold
```

Internally, Native routes reuse the existing scheduler backends. OpenCV replaces the complete operation with `cv::threshold`; it is not inserted into `IExecutionBackend`, because that interface executes arbitrary indexed callbacks while OpenCV implements named vision operations.

## Route learning

A new bounded, sharded route selector records complete end-to-end observations on separate public invocations. It never executes two implementations for one call.

Each eligible route is primed twice without ranking those invocations. Learning then proceeds in balanced rounds. After three observations per active route, the selector uses median/MAD confidence bounds to eliminate routes that are clearly slower. Ambiguous candidates receive additional balanced observations in steps up to the bounded sample window. The provisional winner is never committed immediately: it must pass an independent holdout comparison against the best runner-up. A failed holdout reopens learning with the new evidence, while statistically equivalent routes resolve conservatively in favor of Native Sequential, then Native parallel routes, then external providers.

The selector exposes training medians, MAD, minima, maxima, sample counts, holdout counts, verification failures, current observations, drift state, and route-switch history for publication diagnostics. A small thread-local hot cache bypasses the process-wide selector and internal timing for ordinary stable calls. Hashed generation counters invalidate stale thread-local entries when another thread promotes a different route.

Stable profiles are not trusted forever. Sparse sentinel calls time the current route without putting a timer on every invocation. Two decisive drift observations trigger a current-context ABBA comparison over separate real calls: stable A, challenger A, challenger B, stable B. The switch decision uses those current samples rather than the historical training winner. Periodic ABBA revalidation still starts after eight stable uses and backs off progressively toward 128 uses when the decision remains valid.

Profiles are separated by:

- operation and semantic parameters;
- image-size buckets;
- source and destination strides;
- contiguity and pointer alignment class;
- worker budget;
- available-route mask;
- OpenCV build, configured thread count, optimization, and OpenCL-state fingerprint;
- route-policy configuration and cache generation.

Learning is root-only in v1.5. Automatic calls made from an active SmartParallel execution context use Native Sequential to avoid uncoordinated nested external runtimes.

## Resource model

OpenCV is called once for the complete image using non-owning `cv::Mat` views. SmartParallel does not split the image and invoke internally parallel OpenCV kernels from multiple workers. This avoids multiplying SmartParallel and OpenCV worker teams.

Native ThreadPool, StaticThread, and oneTBB routes partition contiguous pixels or strided rows into bounded chunks and invoke the existing `IExecutionBackend` implementations directly without mutating process-global execution-engine configuration.

## Dependency isolation

The vision module is opt-in. The OpenCV route is additionally opt-in.

```text
SmartParallel::smart_parallel   no OpenCV dependency
SmartParallel::vision           Native-only or Native + OpenCV, according to build
```

`SmartParallelVisionConfig.cmake` is installed separately, so an application using only the core package does not discover or require OpenCV.
