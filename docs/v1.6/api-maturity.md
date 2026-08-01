# SmartParallel v1.6 API maturity

## Stable

- retained v1.0–v1.5 public APIs;
- `SmartParallel::smart_parallel` and optional `SmartParallel::vision` package targets;
- existing default overload behavior, which remains Fast.

## Experimental

- `NumericalPolicy` and `NumericalOptions` overloads;
- `smart::data::View<T, Rank>`, `VectorView`, and `MatrixView`;
- `smart::linalg::{axpy, dot, norm}`;
- `smart::scientific::stencil_2d`;
- numerical execution reports and Vision view adapters.

Experimental APIs are source-available and tested but do not carry an ABI-stability promise in v1.6.

## Internal

- canonical leaf and merge engine;
- compensated and scaled state types;
- numerical capability descriptors;
- canonical plan constants;
- scheduler, SIMD, and provider implementation details.
