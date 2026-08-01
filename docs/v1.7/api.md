# SmartParallel v1.7 public Runtime API

The v1.7 API adds explicit Runtime ownership without removing the established free-function surface.

## Headers

```cpp
#include <smart/runtime/runtime.hpp>
#include <smart/runtime/profile.hpp>
```

Context-aware operations remain declared in their existing module headers, including `smart/execution/parallel.hpp`, `smart/linalg/operations.hpp`, `smart/scientific/stencil.hpp`, and `smart/vision/threshold.hpp`.

## Core types

| Type | Purpose |
|---|---|
| `smart::RuntimeOptions` | Construction-time configuration snapshot. |
| `smart::Runtime` | Owns configuration, scheduler/provider identity, profiles, adaptive evidence, telemetry, and fingerprints. |
| `smart::ExecutionContext` | Copyable handle used to execute through a Runtime. |
| `smart::RuntimeFingerprint` | Canonical Runtime identity and SHA-256 hash. |
| `smart::RuntimeTelemetrySnapshot` | Counters for calls, replay, adaptation, probes, route changes, mutations, and forbidden operation-time profile I/O. |
| `smart::OperationExecutionFingerprint` | Stable identity of the most recently completed semantic operation. |
| `smart::ProfileDatabaseSnapshot` | Read-only value snapshot returned by `Runtime::profiles()`. |

## RuntimeOptions

```cpp
smart::RuntimeOptions options;
options.execution_mode = smart::ExecutionMode::Adaptive;
options.profile_access = smart::ProfileAccess::Disabled;
options.worker_budget = 8;
options.default_numerical_policy = smart::NumericalPolicy::Fast;
options.profile_path = "profiles.json";
options.application_build_identifier = "my-app-2026.08";
options.build_type = "Release";
```

`worker_budget == 0` requests the normal SmartParallel default. Explicit Runtime options are validated and snapshotted during construction.

## Runtime methods

| Method | Contract |
|---|---|
| `context()` | Returns a lightweight context sharing Runtime ownership. |
| `options()` | Returns the immutable construction snapshot. |
| `load_profiles(path)` | Explicitly loads and validates a profile database. |
| `save_profiles(path)` | Explicitly writes the current database atomically. |
| `profiles()` | Returns a value snapshot for inspection. |
| `fingerprint()` | Returns the stable Runtime identity. |
| `telemetry()` | Returns current counters without resetting them. |
| `last_operation_fingerprint()` | Returns the most recently completed semantic-operation identity. |

Profile files are loaded during construction or an explicit `load_profiles` call. Operations never read or write profile files.

## Context-aware operations

v1.7 provides context-aware overloads for:

- `parallel_for`;
- generic Fast `parallel_reduce`;
- AXPY, dot, and norm;
- five-point stencil 2D;
- Vision threshold.

```cpp
auto context = runtime.context();
smart::parallel_for(context, std::size_t{0}, count, callback);
const auto result = smart::linalg::dot(context, x, y, numerical_options);
```

Named semantic operations can participate in persistent exact profiles. Arbitrary callback bodies remain executable through the Runtime but are not assigned portable cross-process semantic identities.

## Default Runtime compatibility

Existing code remains valid:

```cpp
smart::parallel_for(0u, count, callback);
```

The free function uses the process-default Runtime and retains legacy global-configuration behavior. New code that requires isolation, profiles, or Deterministic replay should pass an explicit context.

## Lifetime and concurrency

A copied context owns a shared handle to Runtime state and remains valid after the original `Runtime` wrapper is destroyed. One Runtime supports concurrent calls and synchronizes its mutable evidence. Separate Runtime instances do not share configuration snapshots or profile databases and may collectively oversubscribe the machine.

## Failure behavior

Construction, profile loading, deterministic compatibility checks, and explicit saving report failures with exceptions. Deterministic semantic operations reject missing, Candidate, corrupted, expired, unavailable, or incompatible profiles before modifying destination data. There is no silent fallback to Adaptive execution.

See [Runtime ownership](runtime-ownership.md), [execution modes](execution-modes.md), and [profile compatibility](compatibility.md).
