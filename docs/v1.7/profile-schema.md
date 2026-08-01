# SmartParallel v1.7 profile schema

The experimental profile database uses strict canonical JSON with schema major version 1 and semantic version 1.0.

## Database structure

A database contains:

- schema, semantic, and SmartParallel versions;
- creation metadata;
- environment identity;
- one or more exact operation entries;
- a canonical database SHA-256 identity.

Each operation entry contains five groups of information:

| Group | Examples |
|---|---|
| Semantic identity | Operation name/version, element type, numerical policy. |
| Numerical plan | Evaluation order, accumulation algorithm, canonical plan, capability requirements. |
| Workload identity | Extents, strides, layout, boundary mode, in-place state, semantic constants, exact fingerprint. |
| Execution identity | Route, scheduler plan, exact worker budget, SIMD/provider identity and settings. |
| Evidence and trust | Sample count, medians, variability, confidence, holdout/correctness/authentication gates, Candidate/Approved state, hashes. |

## Supported persistent operations

| Operation identity | Module |
|---|---|
| `smart.vision.threshold` | v1.5 Vision threshold |
| `smart.linalg.axpy` | v1.6 AXPY |
| `smart.linalg.dot` | v1.6 dot product |
| `smart.linalg.norm` | v1.6 norm |
| `smart.scientific.stencil_2d` | v1.6 five-point stencil |

Arbitrary lambdas are not persisted because the Runtime cannot prove their semantic identity or equivalence across builds.

## Canonicalization and integrity

Duplicate keys, excessive depth or size, malformed numeric values, and inconsistent hashes are rejected. Accepted JSON is reserialized canonically before comparison or saving. Entry hashes and the database hash exclude their own hash fields from the corresponding digest input.

## Versioning

Unknown major schemas are rejected. Automatic schema migration is not promised. Operation and plan semantic versions participate in compatibility, so a semantically meaningful change fails exact replay rather than silently reusing stale evidence.

See [compatibility](compatibility.md), [workload identity](workload-identity.md), and [integrity](integrity.md).
