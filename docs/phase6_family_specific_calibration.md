# Phase 6B: Family-specific calibration

Phase 6B connects the Phase 6A workload-family classifier to the analytical
cost model through a bounded correction layer.

The correction is intentionally conservative:

- low-confidence classifications converge to a factor of `1.0`;
- corrections are bounded to `[0.88, 1.18]`;
- the analytical predictor remains the cold-start baseline;
- exact experience calibration and candidate ranking are still applied after
  the family correction.

The policy currently distinguishes:

- compute-heavy workloads, where additional workers are usually useful;
- streaming-memory workloads, where high worker counts can encounter bandwidth
  saturation;
- irregular-memory workloads, where static scheduling receives an imbalance
  penalty;
- branch-heavy workloads, where dynamic scheduling receives a small advantage;
- mixed workloads, where only mild conservative corrections are applied.

Every candidate now exposes the classified family, family confidence, whether
family calibration was applied, and the correction factor used.
