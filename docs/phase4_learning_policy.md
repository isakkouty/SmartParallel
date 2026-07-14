# Phase 4 learning policy refinement

SmartParallel now treats execution history as evidence about outcomes, not as a reward for merely selecting a plan.

## Outcome-aware ranking

Validation paths can record an execution together with the measured best runtime. Each exact plan then tracks decayed runtime, regret, near-optimal success rate, effective sample weight, and timing uncertainty. High regret penalizes a plan; repeated low-regret outcomes increase trust.

## Confidence and uncertainty

Historical influence grows with effective evidence and measurement stability. Timing variance adds a risk penalty, so an unstable candidate does not outrank a consistently good candidate solely because of one fast sample.

## Decay

Historical statistics use exponentially decayed effective weights. Recent outcomes therefore replace stale behavior instead of accumulating forever.

## Similarity transfer

The exact fingerprint remains the primary identity. A bounded secondary signal can transfer outcome quality from nearby workload fingerprints using coarse, non-identifying descriptors: workload kind, iteration scale, working-set scale, object size, callback cost, and variation. Similarity never receives more weight than configured and cannot override strong exact evidence.

## Safety

The analytical model remains the cold-start baseline. Predictive control is still opt-in. Similarity transfer and historical ranking can each be disabled independently through `smart::Config`.
