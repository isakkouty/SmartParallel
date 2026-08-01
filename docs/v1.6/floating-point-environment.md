# SmartParallel v1.6 floating-point environment

Reproducible and Accurate validation assumes:

- the default round-to-nearest floating-point mode;
- no compiler option that permits reassociation or ignores NaN/infinity semantics;
- one unchanged FMA-contraction policy for the binary;
- unchanged flush-to-zero and denormals-are-zero state;
- unchanged excess-precision behavior;
- the same binary and architecture.

`-ffast-math`, `-Ofast`, `/fp:fast`, and equivalent unsafe modes are unsupported for Reproducible/Accurate claims. The v1.6 validation configuration rejects known unsafe flag spellings.

FMA contraction is not promised to match across different compiler builds. A single validated binary remains deterministic because every route executes the same expression and canonical merge plan.

Subnormal inputs follow the active process floating-point environment. SmartParallel does not mutate rounding, FTZ, or DAZ state. Applications that change those states must treat the changed environment as a different reproducibility context.
