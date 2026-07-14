# Phase 10 — Learned Runtime Scaling

Phase 10 replaces unconditional linear extrapolation from a sparse callback
profile with a bounded scaling model.

The model combines:

- workload-family prior growth;
- observed local-versus-distributed profile drift;
- profiling reliability and sample evidence;
- the ratio between profiled callback invocations and full workload size.

Linear scaling remains the neutral fallback. The learned exponent is limited to
`0.90..1.08`, and the final correction factor is limited to `0.72..1.35`.
This keeps cold-start behavior safe while allowing streaming workloads to model
sublinear saturation and irregular workloads to model mild superlinear growth.

Every candidate exposes scaling diagnostics through `PlanCostEstimate`.
