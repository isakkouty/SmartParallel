# SmartParallel v1.7 migration from v1.6

Existing v1.6 source code does not require changes. Adoption can be incremental.

## Level 0: remain on free functions

```cpp
smart::parallel_for(0u, count, callback);
```

The call uses the process-default Runtime and preserves legacy global configuration.

## Level 1: adopt explicit ownership

```cpp
smart::RuntimeOptions options;
options.worker_budget = 8;
options.application_build_identifier = "my-app-2026.08";
smart::Runtime runtime(options);

auto context = runtime.context();
smart::parallel_for(context, 0u, count, callback);
```

This isolates configuration from unrelated global changes and enables Runtime-scoped diagnostics.

## Level 2: adopt Adaptive restart evidence

1. Choose a stable `application_build_identifier`.
2. Configure `ProfileAccess::ReadWrite` for calibration or explicit export.
3. Run representative named semantic operations.
4. Save Candidate evidence explicitly.
5. Review integrity, compatibility, correctness, route authentication, and confidence.

A compatible loaded Candidate may warm-start the first Adaptive semantic call after restart.

## Level 3: adopt Deterministic deployment

1. Approve reviewed Candidate evidence into a distinct Approved file.
2. Distribute the Approved file ReadOnly.
3. Construct a Deterministic Runtime with the exact same build identifier, worker budget, numerical policy, workload shape, and provider environment.
4. Compare Runtime, operation, and run-manifest fingerprints in deployment validation.

```cpp
smart::RuntimeOptions options;
options.execution_mode = smart::ExecutionMode::Deterministic;
options.profile_access = smart::ProfileAccess::ReadOnly;
options.profile_path = "approved_profile.json";
options.worker_budget = 8;
options.application_build_identifier = "my-app-2026.08";
smart::Runtime runtime(options);
```

## Deployment checklist

- Treat profile files as versioned release artifacts.
- Never overwrite Approved evidence in place during calibration.
- Keep worker budget and build identity explicit.
- Expect exact compatibility rejection after meaningful build, workload, provider, or floating-environment changes.
- Retain the v1.6 numerical policy choice; Deterministic execution does not replace it.

Rollback is straightforward: keep using the free-function path or switch an explicit Runtime back to Adaptive/Disabled while retaining the same operation APIs.
