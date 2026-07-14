# Phase 6D — Bounded Similarity Transfer and V1 Model Gates

Phase 6D makes workload similarity an explicit, inspectable concept instead of
leaving it embedded inside the experience database.

## Similarity report

`compare_fingerprints()` returns both the total similarity and its components:
iteration scale, represented working set, object size, function cost, and
variation. Workloads of incompatible kinds receive zero similarity.

## Safety rules

Similarity can only influence residual correction when:

- exact history is absent or weak;
- total similarity meets the configured threshold;
- the source plan has outcome history;
- the transferred weight remains below the configured similarity cap;
- the transferred correction remains inside the similarity-specific bounds.

Exact experience remains stronger than transferred experience. A distant or
incompatible workload contributes nothing.

## V1 model gates

The V1 model should be frozen only after the following validation goals are
met across repeated holdout runs:

- median selected-plan regret below 3%;
- mean selected-plan regret below 8%;
- at least 65% of decisions within 3% of the measured best;
- no unexplained worst-case regret above 30%;
- learned final-round regret no worse than cold-start regret;
- all focused tests and validation executables pass.
