# SmartParallel v1.7 Runtime and operation fingerprints

Fingerprints turn configuration and execution identity into stable, comparable evidence.

## Fingerprint hierarchy

| Identity | Representative fields |
|---|---|
| Runtime | SmartParallel/build identity, execution mode, profile access, worker budgets, numerical default, scheduler capabilities, hardware/build identity, floating-point environment, application build identifier. |
| Workload | Semantic operation, version, element type, policy, extents, strides, layout, boundary mode, in-place state, semantic constants. |
| Profile database | Canonical database content and entry identities. |
| Operation execution | Runtime/workload/profile hashes, numerical plan, route, scheduler, workers, provider/SIMD identity, warm-start and Deterministic state. |
| Experiment manifest | Application identity, inputs, dimensions, policy, output digest, Runtime/profile/operation identities, telemetry, completion status. |

## Stable versus volatile fields

Stable hashes exclude values that should differ between equivalent runs:

- memory addresses;
- process and thread IDs;
- timestamps;
- output paths;
- measured durations.

These values may appear in diagnostic logs but do not define reproducible identity.

## API

```cpp
const auto runtime = runtime_instance.fingerprint();
const auto operation = runtime_instance.last_operation_fingerprint();

auto telemetry = runtime_instance.telemetry();
```

Each fingerprint exposes canonical identity text and a SHA-256 hash. The operation fingerprint is updated after a supported semantic operation completes.

## Interpretation

Equal hashes mean that the fields included in the canonical identity agree. They do not prove organizational authorization, code provenance, or safety. Use Approved profile state and controlled artifact distribution for authorization; use signatures outside v1.7 when authorship must be cryptographically proven.
