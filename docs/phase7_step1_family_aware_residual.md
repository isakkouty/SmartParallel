# Model Refinement Step 1: Family-Aware Residual Correction

SmartParallel keeps the analytical cost model as its cold-start baseline and
uses measured prediction errors only as a bounded correction. This refinement
makes the correction policy workload-family aware.

Compute-heavy workloads may use the full configured historical weight when
measurements are stable. Streaming-memory, irregular-memory, mixed, and
unknown workloads use progressively more conservative influence because their
runtime is more sensitive to system contention and short-term noise.

Corrections are blended in logarithmic space. This treats equal relative
under-prediction and over-prediction symmetrically and avoids an arithmetic
bias toward large upward adjustments. Low-confidence family classifications
smoothly fall back toward the global policy rather than switching abruptly.

New diagnostics expose the effective family weight and correction bounds for
each candidate. The focused test verifies that identical evidence produces a
smaller correction for streaming-memory work than for compute-heavy work.
