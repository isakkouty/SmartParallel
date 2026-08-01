# SmartParallel v1.7 security and trust boundaries

v1.7 provides strict parsing, integrity detection, explicit approval state, and fail-closed deterministic compatibility. It is not a general security sandbox or signature system.

## Controls provided

| Control | Purpose |
|---|---|
| Bounded strict JSON parser | Rejects excessive size/depth, malformed data, duplicate keys, and numeric overflow. |
| Canonical serialization | Gives accepted content one stable representation. |
| Entry/database SHA-256 | Detects changed or inconsistent content. |
| Candidate/Approved states | Separates measured evidence from explicit deployment authorization. |
| Exact compatibility | Prevents replay under mismatched workload, build, environment, worker, scheduler, or provider identity. |
| Atomic saving | Avoids replacing a valid destination with incomplete temporary content. |
| ReadOnly deployment | Prevents operation-driven profile mutation. |
| Telemetry invariants | Exposes unexpected adaptive maintenance during replay. |

## Threat boundary

These controls do not protect against a malicious party that can replace both an artifact and its expected hash, compromise the running process, alter the compiler or dependency chain, or authorize an unsafe profile. They do not provide cryptographic authorship, sandboxing, remote attestation, regulatory approval, or safety certification.

## Deterministic meaning

Deterministic mode reproduces an Approved execution identity only under exact compatible conditions. It does not make an incompatible environment safe, and Fast numerical policy does not become bitwise reproducible merely because route selection is fixed.

## Operational guidance

- Keep Candidate and Approved artifacts separate.
- Distribute Approved profiles through the same controlled channel as application binaries.
- Pin and verify the expected profile database hash.
- Use ReadOnly access in production.
- Recalibrate and reapprove after meaningful build, workload, numerical, provider, or environment changes.
- Add external digital signatures when authorship must be proven.
