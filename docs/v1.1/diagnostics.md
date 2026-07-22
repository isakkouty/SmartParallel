# Diagnostics

> **Runtime documentation:** SmartParallel v1.1 behavior, retained by the current [v1.3 portability release](../v1.3/README.md).

## Latest-call diagnostics

`ParallelForProfileDiagnostics` reports sampling, cache lookup, workload analysis, decision time, stable-plan reuse, backend selection, and fast-path behavior. `ParallelForNestedDiagnostics` reports nested policy and context details for the latest call.

```cpp
auto profile = smart::global_last_parallel_for_profile_diagnostics();
auto nested = smart::global_last_parallel_for_nested_diagnostics();
```

## Structured nested trace

Enable trace collection before execution:

```cpp
smart::global_config().enable_nested_execution_trace = true;
smart::clear_nested_execution_trace();

run_workload();

smart::write_nested_execution_trace_csv("nested_trace.csv");
```

Trace fields cover lineage, callsite identity, phase, requested/actual backend, backend authentication, policy, mechanism, budgets, leases, chunks, helpers, cancellation, and causal completion timing.

## Interpreting completion timing

- `helper_in_flight_work_drain_ms`: useful work still running after no unclaimed chunks remain.
- `helper_actual_blocking_wait_ms`: time the caller actually blocked.
- `helper_completion_signal_to_wake_ms`: notification-to-wake delay only when a real wait occurred.
- `helper_completion_epilogue_ms`: post-completion cleanup.

A long work-drain value indicates a straggler or coarse task, not necessarily a notification defect.

## Performance warning

Tracing and detailed timing add synchronization and I/O overhead. Use them for diagnosis and correctness validation, then disable them for ordinary performance measurements.
