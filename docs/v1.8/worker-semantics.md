# SmartParallel v1.8 — Requested, preferred, granted, capped, and observed workers

The fields are intentionally distinct:

- **Minimum workers:** smallest legal flexible grant.
- **Preferred workers:** useful concurrency estimated for the operation.
- **Maximum workers:** hard Runtime and operation ceiling.
- **Requested workers:** immediate admission target.
- **Granted workers:** permits actually admitted.
- **Scheduler concurrency cap:** maximum participation configured in the selected scheduler.
- **Observed participating workers:** measured participants when observable.
- **Exact-grant requirement:** whether a smaller grant is forbidden.

Adaptive operations may accept a grant between minimum and maximum and finalize the execution plan for that grant. Deterministic profiles persist an exact contract in which all grant bounds equal the approved worker count.
