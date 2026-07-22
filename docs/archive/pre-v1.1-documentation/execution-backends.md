# Execution backends

All public CPU execution mechanisms participate in the same nested-session correctness contract: acquired participant width bounds actual execution width, permits remain owned for participant lifetime, and exceptions release permits before propagation.

## Sequential

Runs the range on the caller. It owns one session participant when entered through a root session.

## ThreadPool

Uses SmartParallel's persistent global pool with dependency-local cooperative helping. Helpers own explicit session permits, release them before publishing completion, and queued helpers can be cancelled when no useful work remains.

## StaticThread

Creates a bounded fixed team and assigns contiguous ranges. `StaticChunks` is routed through this backend rather than bypassing session accounting. If thread creation fails after a partial team was created, already-created threads are cancelled/joined before the exception is propagated.

Automatic StaticThread candidacy remains disabled by default.

## oneTBB

Executes inside a `tbb::task_arena` constrained to the acquired participant width. An existing TBB arena is reused only when its concurrency does not exceed that width; otherwise SmartParallel enters a narrower arena.

A release test can require real oneTBB discovery instead of accepting fallback:

```bat
scripts\validation\run_nested_release_validation.bat 11 tbb
```

CMake option `SMARTPARALLEL_REQUIRE_TBB=ON` fails configuration when TBB is unavailable.

## Consistency boundary

The configured nested budget is per root session. Independent external roots can collectively use more participants than one root budget, subject to backend capacity. Strict process-wide admission and fairness are not v1.1 guarantees.
