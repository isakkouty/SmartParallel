# SmartParallel v1.1 nested execution update

This update hardens the existing `NestedExecutionSession` architecture without replacing it.

## Release-blocking fixes

### Predicate-safe helper completion

The cooperative ThreadPool path no longer polls a condition variable with a nominal 100 microsecond timeout. The helper count is changed and tested under the same mutex used by the waiter, so the final notification cannot be lost between predicate evaluation and sleep.

Trace output now separates:

- `helper_retire_tail_ms`: time from the caller exhausting unclaimed chunks until all dependency helpers complete;
- `helper_completion_signal_to_wake_ms`: time from the final completion transition to the waiter's return;
- `helper_wait_count`: whether the caller had to block after dependency-local helping.

### Accurate helper permit lifetime

Each ThreadPool helper owns one root-session permit. The permit is explicitly released before that helper publishes completion. This prevents `execute()` from returning while a worker closure still retains a permit.

Helper queue publication is also exception-safe. A failure after some helpers have been submitted cancels the region, drains already-published dependency jobs, releases every permit, and rethrows through the normal work-region exception path.

### Explicit participant ownership

Being a global ThreadPool worker no longer implies ownership of a permit in an unrelated nested session. Session ownership is tracked per thread and per `NestedExecutionSession`.

Execution width is clamped before permits are acquired. A one-iteration region therefore reserves one participant rather than a nominal four-worker team.

### Collision-safe frozen plans

Per-root plan snapshots use the complete contextual key rather than only its hash. Hash collisions can no longer select another callsite's frozen plan.

## Profiling behavior

When profile caching and nested root telemetry are enabled, a cold nested root performs one conservative exactly-once execution:

```text
root: sequential online learning
descendants: sequential online learning
next invocation: cached frontier decision
```

This avoids measuring nested callbacks under a provisional plan that differs from the plan ultimately executed. The learned per-depth profiles are used on later invocations. Stable cached plans are periodically revalidated so phase-changing workloads are not permanently locked to one decision.

Non-nested profiling retains the established regional sampler.

## Regular four-level policy

For the benchmark shape `2 x 3 x 4 x 192` with a four-participant root budget, the normal automatic plan remains:

```text
L1 sequential: underfilled outer level
L2 sequential: underfilled outer level
L3 parallel: four coarse subtrees
L4 sequential: descendant of the selected frontier
```

For a rectangular dependency-free nest, `smart::parallel_for_nd` remains the lower-overhead fast path.

## Budget semantics

`nested_root_concurrency_budget` limits one nested root session. Independent roots started by separate external threads each own an independent session and can collectively use more than that value. A process-wide fairness/admission controller is outside the v1.1 scope.

## Validation commands

Windows, from the repository root:

```bat
scripts\validation\run_nested_release_validation.bat 31
scripts\validation\run_nested_release_validation.bat 3 trace
```

Linux/macOS:

```bash
chmod +x scripts/validation/run_nested_release_validation.sh
./scripts/validation/run_nested_release_validation.sh 31
./scripts/validation/run_nested_release_validation.sh 3 trace
```

The second command writes a separate trace-run prefix and does not overwrite the 31-repetition performance files.

## Remaining v1.2 candidates

- Bounded descendant borrowing when a strict frontier has idle capacity.
- Process-wide fairness/admission across independent roots.
- Explicit source-level callsite tokens for reusable functor types.
- More detailed per-helper CPU-time and OS scheduling telemetry.
- Native oneTBB stress validation on every supported platform.
