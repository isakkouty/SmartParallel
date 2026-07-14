# V1 Phase 1 — MR-1 Sensor-Layer Consolidation

## Purpose

This merge request improves the evidence available before an execution decision.
It does not change execution policy.

## Structural observations

The workload model now records storage topology conservatively. Contiguous and
random-access properties are reported only when they can be established from the
container interface. Unknown storage remains unknown.

The analyzer exposes continuous measurements including represented input bytes,
unique input elements, pair-workload reuse factors, iterations per hardware unit,
cache residency ratios, and element/cache-line relationships.

`working_set_bytes` remains as a compatibility alias. It means represented input
storage, not measured callback memory traffic.

## Function-profile observations

The profiler calibrates its timer once per process and adapts batch size until the
measurement is sufficiently above the timer floor or a safety bound is reached.
Profiling is bounded by time, callback-invocation, and sample-count budgets.

The primary work estimate uses a trimmed mean. Mean, median, standard deviation,
coefficient of variation, p95, tail ratio, and maximum remain available for
inspection. Confidence is conservative and records whether a measurement reached
the requested relative-error target.

## Non-goals

This change does not infer exact memory traffic, arithmetic intensity, branchiness,
vectorization potential, or cache misses. It does not tune decisions, workers,
chunks, or execution backends.
