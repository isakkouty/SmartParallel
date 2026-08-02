# Release notes

SmartParallel v1.8.0 adds process-level CPU governance and deterministic resource admission.

Major changes:

- operation-specific minimum/preferred/maximum concurrency requests;
- flexible Adaptive partial grants with plan finalization for the actual grant;
- exact fail-closed Deterministic grants;
- shared multi-Runtime budgets;
- move-only exception-safe leases;
- direct cancellation notification;
- FIFO admission with bounded bypass, aging, and oldest-request reservation;
- nested parent-lease reuse without independent root acquisition;
- effective CPU-capacity diagnostics;
- honest oneTBB task-arena and OpenCV containment reporting;
- publication benchmarks with alternating order, real participation measurement, correct throughput units, preserved outliers, and 95% confidence intervals.

Rodinia HotSpot is removed from v1.8 and preserved separately for `SmartParallel v1.9.0 — Rodinia HotSpot Integration`.

Roadmap:

```text
v1.8.0 — Governed Scientific Execution
Process-level CPU governance and deterministic resource admission.

v1.9.0 — Rodinia HotSpot Integration
First complete real-world external application integration.

v1.9.x — Additional application integrations
Each external tool is integrated and validated as a separate release.
```
