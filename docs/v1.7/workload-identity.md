# SmartParallel v1.7 exact workload identity

Persistent profiles are keyed by semantic meaning and exact data shape, not by a loose operation name alone.

## Included identity

The workload fingerprint includes:

- stable operation name and semantic version;
- element type;
- numerical policy;
- logical extents;
- element or byte strides;
- layout;
- boundary mode;
- in-place state;
- operation constants.

Examples of semantic constants include AXPY alpha, threshold values, and five-point stencil coefficients.

## Exact matching

Deterministic lookup is exact. Any meaningful change rejects the profile, including:

- a different vector length;
- a changed matrix row stride;
- switching from contiguous to padded layout;
- changing the numerical policy;
- changing a boundary condition;
- changing AXPY alpha or stencil coefficients;
- changing in-place behavior.

No nearest-neighbor, bucketed, or fuzzy deterministic matching is performed.

## Why exact identity matters

A saved route can be correct and fast for one shape but invalid, unavailable, or numerically different for another. Exact identity prevents a superficially similar workload from inheriting evidence that was never measured for it.

Adaptive execution may ignore incompatible evidence and learn the current context. Deterministic execution fails closed instead.
