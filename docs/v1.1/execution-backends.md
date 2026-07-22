# Execution backends

> **Runtime documentation:** SmartParallel v1.1 behavior, retained by the current [v1.3 portability release](../v1.3/README.md).

All backends participate in the same nested-session contract: effective execution width is bounded by acquired participants, exceptions release resources before propagation, and traces report the backend that actually executed.

## Sequential

Runs the range on the caller. It is the preferred plan for insufficient work or exhausted nested budgets.

## ThreadPool

Uses SmartParallel's persistent global pool, dynamic chunks, dependency-local helping, explicit participant leases, and cancellation of queued helpers that are no longer useful.

## StaticThread

Creates a bounded fixed team and assigns static ranges. It is useful for deterministic comparison and some uniform workloads. Automatic StaticThread candidacy is disabled by default.

## oneTBB

Uses `tbb::parallel_for` inside a `tbb::task_arena` constrained to the acquired participant width. SmartParallel can select oneTBB automatically when it is available, and release validation can require genuine oneTBB discovery.

## Auto resolution

`ExecutionEngineType::Auto` allows the decision system and optional warm-up calibration to choose a backend. The final real-world benchmark freezes the selected backend after warm-up for deterministic steady-state measurements.

## Availability

When oneTBB is unavailable and not required, SmartParallel builds the ThreadPool and StaticThread paths and emits a CMake warning. Set `SMARTPARALLEL_REQUIRE_TBB=ON` when fallback is unacceptable.
