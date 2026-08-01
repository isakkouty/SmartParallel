# SmartParallel v1.7 known limitations

The v1.7 release claim is intentionally narrower than “reproducible everywhere.”

## Resource governance

- Independent Runtime instances are not coordinated by one process-wide CPU governor.
- Multiple Runtimes can collectively oversubscribe the machine.
- Affinity, NUMA placement, cancellation, and deadlines are not part of the Runtime contract.

## Reproducibility scope

- Cross-architecture, cross-compiler, cross-standard-library, cross-provider, and cross-floating-environment bitwise identity is not promised.
- Deterministic + Fast fixes the approved execution identity but does not strengthen Fast floating-point semantics.
- Profiles match exact workload identities; fuzzy or nearest-neighbor deterministic matching is not provided.

## Persistence and trust

- Concurrent multi-process profile writers are unsupported.
- Automatic profile schema migration is not promised.
- SHA-256 detects modification but does not prove authorship or approval.
- Digital signatures, remote profile services, and centralized policy authorization are outside v1.7.

## Operation coverage

Persistent semantic profiles are limited to threshold, AXPY, dot, norm, and stencil 2D. Arbitrary callbacks do not receive portable cross-process identities.

OpenMP, BLAS, FFT, GPU, MPI, C, and Python APIs remain outside the release. Public named execution scopes and a general execution-tree export are deferred.

## API maturity

Scientific, profile, and Runtime APIs are experimental and do not promise a stable binary ABI. Existing v1.0–v1.6 source APIs remain available, but serialized profile compatibility is governed by explicit schema and semantic versions rather than an indefinite compatibility promise.
