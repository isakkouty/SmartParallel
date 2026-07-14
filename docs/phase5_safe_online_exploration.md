# Phase 5 — Safe Online Exploration

SmartParallel can optionally test a near-best alternative plan so that experience does not permanently lock the runtime into a local optimum.

Exploration is disabled by default. When enabled, a candidate is eligible only when it is sufficiently confident and its ranking score is within a configured percentage of the exploitation winner. A deterministic bounded trial decides whether an experiment occurs.

After an exploratory execution, SmartParallel compares the observed runtime with the exploitation plan's expected runtime. Harmful experiments trigger a cooldown, preventing repeated losses. The normal predictive-confidence gate remains authoritative, so exploration never bypasses predictive-control safety.

Relevant configuration fields are:

- `enable_online_exploration`
- `exploration_probability`
- `maximum_exploration_probability`
- `maximum_exploration_score_gap_percent`
- `minimum_exploration_confidence`
- `minimum_exploration_candidate_confidence`
- `maximum_exploration_regret_percent`
- `exploration_cooldown_calls`
