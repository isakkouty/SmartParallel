# Current Phase

SmartParallel is collecting production-safe DecisionContext data for utility learning.

Current status:

- Production decisions do not use the legacy runtime predictor.
- Legacy runtime estimates are retained only in offline validation datasets as a frozen comparison baseline.
- Utility-model training requires at least 100 calibration workloads.
- With fewer than 100 groups, the launcher audits the dataset and reports `MODEL NOT TRAINED`.
- Promotion requires lower holdout mean regret than the offline baseline and no increase in decisions with more than 20% regret.

Run the complete workflow with:

```bat
run_v1_phase1.bat
```

## Latest implementation step: deployable model artifact

The Phase 1 utility learner now saves a versioned `.spm` artifact, and the C++ runtime includes validated save/load and feature-scaling support. This completes the persistence foundation; the next V1 step is wiring an approved artifact into a hybrid runtime decision provider and then validating it in three existing projects.

## V1 hybrid runtime policy

The persisted utility artifact is now connected to `DecisionEngine` behind an
opt-in safety gate. A promoted, schema-compatible, sufficiently confident model
may select Sequential, ThreadPool, or oneTBB; otherwise the existing analytical
and historical provider remains authoritative. The runtime decision report
records load, compatibility, promotion, confidence, application, and fallback
reason diagnostics.
