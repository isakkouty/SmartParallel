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
