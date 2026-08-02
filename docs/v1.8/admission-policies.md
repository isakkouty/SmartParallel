# SmartParallel v1.8 — Admission policies

A request distinguishes three worker bounds:

```text
minimum <= preferred <= maximum
```

`requested_workers` is the immediate target, normally equal to `preferred_workers` for flexible Adaptive work.

Supported policies are:

- `FailImmediately`: return `WouldBlock` when capacity is not immediately available;
- `Wait`: wait until the request can be granted, cancelled, or the governor shuts down;
- `WaitUntilDeadline`: wait only until an absolute steady-clock deadline.

Requests above the permanent governor budget are `ImpossibleRequest` and never wait indefinitely.
