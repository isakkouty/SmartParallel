# SmartParallel documentation

## Current release

The current release is **SmartParallel v1.4.0**, focused on adaptive parallel algorithms implemented on the existing scheduler and nested runtime.

- [v1.4 overview](v1.4/README.md)
- [v1.4 API and correctness contracts](v1.4/api.md)
- [v1.4 architecture and hot dispatch](v1.4/architecture.md)
- [v1.4 benchmark results](v1.4/benchmarks.md)
- [v1.4 benchmark methodology](v1.4/benchmark-methodology.md)
- [v1.4 benchmark reproduction](v1.4/benchmark-reproduction.md)
- [v1.4 validation](v1.4/validation.md)
- [v1.4 known limitations](v1.4/known-limitations.md)
- [v1.4 release notes](v1.4/release-notes.md)

SmartParallel v1.3 remains the portability and CI foundation:

- [v1.3 overview](v1.3/README.md)
- [GitHub Actions and vcpkg setup](v1.3/ci-and-github-setup.md)
- [Cross-platform installation](v1.3/installation.md)

## Runtime and scheduler documentation

SmartParallel v1.4 retains the stabilized v1.1 runtime behavior. The detailed scheduler documentation remains under [`docs/v1.1/`](v1.1/README.md).

- [Getting started](v1.1/getting-started.md)
- [API](v1.1/api.md)
- [Architecture](v1.1/architecture.md)
- [Automatic loop optimization](v1.1/automatic-loop-optimization.md)
- [Nested parallelism](v1.1/nested-parallelism.md)
- [Runtime learning](v1.1/runtime-learning.md)
- [Execution backends](v1.1/execution-backends.md)
- [Configuration](v1.1/configuration.md)
- [Diagnostics](v1.1/diagnostics.md)
- [Benchmarks](v1.1/benchmarks.md)
- [Known limitations](v1.1/known-limitations.md)

## Historical material

- [`docs/v1.0/`](v1.0/README.md) summarizes the archived v1.0 release.
- [`docs/archive/`](archive/README.md) preserves pre-release, engineering, and validation documents for traceability. Archived files are not authoritative for current behavior.
