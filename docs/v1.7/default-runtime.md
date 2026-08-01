# SmartParallel v1.7 backward-compatible default Runtime

v1.7 preserves existing free-function source compatibility by routing legacy calls through a process-default Runtime.

## Existing code remains valid

```cpp
smart::parallel_for(0u, count, callback);
```

The default path retains the established mutable global-configuration behavior so v1.0–v1.6 applications do not require a migration merely to build v1.7.

## Explicit versus default Runtime

| Property | Process-default Runtime | Explicit Runtime |
|---|---|---|
| Existing free functions | Yes | Context overloads |
| Legacy global configuration | Remains effective | Snapshotted at construction; later changes ignored |
| Isolation from unrelated code | Limited | Yes |
| Persistent profile policy | Not the recommended deployment surface | Explicitly configured |
| Deterministic deployment | Possible only through explicit configuration paths | Recommended |
| Multiple independent configurations | No | Yes |

## Guidance

Keep the default Runtime for compatibility-oriented code and simple adaptive execution. Construct an explicit Runtime when an application needs:

- configuration isolation;
- a specific worker budget or numerical default;
- ReadOnly or ReadWrite profile behavior;
- stable application build identity;
- Runtime/operation fingerprints;
- exact Deterministic replay.

The default Runtime is a compatibility bridge, not a hidden global replacement for explicit ownership.
