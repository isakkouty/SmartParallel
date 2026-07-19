# Profiling and experience

## Automatic callback profiling

When no reliable cached profile exists, `parallel_for` samples a bounded number of indices. Sampling stops after the minimum sample count once a timing signal is observed, or at the maximum sample budget. Sampled indices are recorded and omitted from the subsequent execution gaps, preserving exactly-once semantics.

The profile records average, median, trimmed mean, p95, maximum, tail ratio, instability, confidence, elapsed profiling time, and estimated parallel worthiness.

## Profile cache

Profiles are keyed by callable type, index representation, and iteration-count bucket. Cached data is blended with new observations. Cheap callbacks require repeated independent confirmation before the sequential fast path activates, and periodic revalidation prevents a stale sequential classification from becoming permanent.

## Experience database

When enabled, completed executions can update historical evidence for workload fingerprints and plans. Persistence is off by default; enabling it writes to the configured experience database path. History decays over time and its ranking authority is bounded.

## Shadow and predictive modes

Predictive shadow mode measures learned recommendations without handing them control. Predictive decisions remain disabled by default. This separation is important for validating a model before promotion.

## Exploration

Online exploration is opt-in and disabled by default. It is restricted to near-best alternatives with sufficient confidence and includes regret limits and cooldown behavior.
