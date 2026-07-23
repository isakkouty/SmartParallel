# SmartParallel v1.4.0 algorithm benchmarks

`smartparallel_v140_algorithm_benchmarks` benchmarks all fourteen v1.4 API families through sixteen unary/binary cases against sequential references and available SmartParallel backends. Every repetition is checksum validated.

Build directly with:

```text
-DSMARTPARALLEL_BUILD_V140_ALGORITHM_BENCHMARKS=ON
```

The recommended complete Windows workflow is:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v14_algorithm_release_validation.bat 7
```

See [`docs/v1.4/benchmark-methodology.md`](../../docs/v1.4/benchmark-methodology.md) for the schema and measurement rules.
