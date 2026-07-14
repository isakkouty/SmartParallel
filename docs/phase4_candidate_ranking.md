# Phase 4 candidate ranking

Phase 4 adds an experience-aware ordering layer above the analytical runtime
model. It does not use Python, a neural network, or an opaque trained model.

For every candidate, SmartParallel first computes a normalized analytical
score from the predicted runtime. When the experience database contains enough
stable samples for the exact workload fingerprint and exact execution plan, it
also computes a historical score relative to the best observed plan.

The final score is a confidence-weighted blend. Lower is better. Missing,
unstable, or low-sample history is ignored, so cold-start behavior remains the
Phase 3 analytical model.

The candidate report exposes:

- `analytical_rank_score`
- `historical_rank_score`
- `ranking_score`
- `ranking_history_weight`
- `ranking_samples`
- `experience_rank_used`

This design optimizes plan ordering directly while keeping every decision
inspectable.
