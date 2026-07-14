# Phase 4: Ranking evolution validation

The analytical model is the cold-start policy. Once exact workload/plan
measurements exist, the candidate ranker blends analytical ordering with
historical ordering. Historical data is accepted only after the configured
minimum sample count and is weighted by measurement confidence.

The ranking-evolution validator measures all candidates repeatedly rather than
only executing the currently selected plan. This controlled exploration gives
each candidate comparable history and lets us answer a concrete question:

> Does selected-plan regret fall after repeated observations?

The validator reports cold-start regret, final-round regret, exact winner
accuracy, near-optimal accuracy, ranking history weight, and sample count. It
also verifies that saved experience produces the same ranking after reload.
