# SmartParallel v1.5 adaptive execution-route benchmark

This suite evaluates the first semantic operation, exact one-channel `uint8_t` thresholding, across:

- direct sequential reference;
- direct OpenCV API;
- SmartParallel Auto;
- forced Native Sequential, ThreadPool, StaticThread, and oneTBB routes;
- forced SmartParallel OpenCV.

The release workflow validates exact output, backend authentication, identical source/destination allocations, Native SIMD quality, balanced route learning, deployment-regime adaptation, settled route quality, and stable Auto overhead.

Run the publication matrix on Windows/MSVC with:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v15_adaptive_routes_release_validation.bat 31
```

See:

- [public results](../../docs/v1.5/benchmarks.md);
- [methodology](../../docs/v1.5/benchmark-methodology.md);
- [reproduction guide](../../docs/v1.5/benchmark-reproduction.md).

Performance results are machine-specific and are not CI merge gates.
