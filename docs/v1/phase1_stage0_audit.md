# SmartParallel V1 — Phase 1, Stage 0 Audit

## Scope

This audit traces the current pre-execution pipeline and records semantic and correctness issues that must be resolved before the Phase 1 sensor layer is expanded.

## Current lifecycle

For `smart::for_each`, the current implementation performs:

1. clear timing diagnostics when enabled;
2. build a `Workload` from the container;
3. analyze structural workload information;
4. profile the callback on selected indices;
5. pass the workload, analysis, and optional profile to `DecisionEngine`;
6. execute the full workload using the selected plan;
7. record the selected plan and measured execution time when experience is enabled.

`smart::for_each_pair` follows the same pipeline after flattening the Cartesian iteration space into a single index range. `smart::parallel_for` currently skips function profiling and decides from workload structure alone.

## Profiling semantics

The current profiler invokes the real callback on elements from the real user container. The execution phase then processes the complete range again.

Therefore, sampled elements are currently invoked at least twice:

- once during function profiling;
- once during normal execution.

For in-place element transforms, this changes program results. For callbacks with external side effects, it duplicates those effects. The current implementation therefore does not provide exactly-once callback execution when automatic profiling is active.

This is a correctness issue, not merely a profiling-quality issue.

## Recommended semantic direction

The smallest practical V1 direction is:

- profile copied sample values rather than live container elements when the element type and callable can be copied safely;
- mark the function profile unavailable when safe isolated sampling is not possible;
- define the callback contract as independent per-element work without unsynchronized external side effects;
- keep the public `smart::for_each` API unchanged.

This avoids mutating the real workload during profiling. It does not make arbitrary externally side-effecting callbacks safe; such callbacks are already incompatible with unconstrained parallel execution.

A sampled-index skip system was considered and rejected for the first implementation because it would add per-iteration bookkeeping and backend-specific sparse-range execution complexity.

## Overflow audit

The following calculations were unsafe in the Beta implementation:

- pair iteration count: `a.size() * b.size()`;
- represented byte count: `dimension.size * dimension.object_size`;
- aggregate represented byte count;
- static partition bounds computed as `(total * worker) / workers`.

Stage 0 now uses saturating arithmetic for workload magnitude calculations and quotient/remainder partitioning for static ranges.

Saturation is explicitly recorded in `Workload` and `WorkloadAnalysis`.

## Terminology

`working_set_bytes` is currently derived from represented container storage. It is not a measurement of bytes actually touched by the callback.

The field remains for compatibility during Stage 0, but its comment and diagnostics must describe it as an estimate. Stage 1 will introduce a technically clearer continuous observation while compatibility fields remain derived views.

## Units

- iteration counts: number of logical callback invocations;
- object sizes: bytes represented by the container value type;
- `working_set_bytes`: estimated represented bytes;
- profiler timing fields: milliseconds per callback iteration;
- execution and timing-report fields: milliseconds.

## Stage 0 code changes

- added saturating `size_t` addition and multiplication helpers;
- made pair workload iteration counts overflow-safe;
- made represented-byte aggregation overflow-safe;
- added saturation flags to workload and analysis structures;
- switched element representation sizing to the container `value_type` instead of proxy/reference expression size;
- made static partitioning safe for zero job counts and large ranges.

## Remaining Stage 0 blocker

Exactly-once behavior is not yet repaired in this increment. The next implementation step must isolate profiling from live user data before Stage 1 begins.

## Profiling-isolation resolution

The Stage 0 blocker has now been addressed for element mutation.

`smart::for_each` and `smart::for_each_pair` no longer profile callbacks on the live elements stored in the user containers. Automatic profiling now:

1. verifies that the callable and sampled value types are copy-constructible;
2. copies the callable into a profiling-only instance;
3. copies each sampled value into temporary storage;
4. invokes the profiling callable only on those temporary values;
5. marks the profile unavailable when isolated value sampling cannot be performed.

This prevents profiling from applying in-place transformations twice to the real workload. The execution phase remains the only phase that mutates the user containers.

The callback contract still requires independent per-element work without externally visible write side effects during profiling. Copying a callable cannot isolate reference captures, global state, I/O, or other external effects. This limitation is explicit and will remain part of the V1 API contract unless a future opt-in profiling API provides stronger user guarantees.

Function profiles now record whether samples were obtained directly or from isolated copies, together with a reason when an isolated profile is unavailable.

Pair workload iteration overflow now fails explicitly before profiling or execution rather than attempting to execute a saturated range.
