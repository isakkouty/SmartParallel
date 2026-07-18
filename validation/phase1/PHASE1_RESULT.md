# SmartParallel V1 Phase 1 Result

**Utility-model status: SHADOW ONLY — NOT PROMOTED**

| Metric | Offline runtime baseline | Utility model | Delta |
|---|---:|---:|---:|
| mean_regret_percent | 2.75406 | 42.5363 | +39.7822 |
| median_regret_percent | 0 | 0 | +0 |
| p90_regret_percent | 1.90836 | 34.6733 | +32.7649 |
| p95_regret_percent | 10.8669 | 162.929 | +152.062 |
| p99_regret_percent | 35.7761 | 533.275 | +497.499 |
| worst_regret_percent | 42.0034 | 625.862 | +583.859 |
| catastrophic_rate_over_20_percent | 5.88235 | 17.6471 | +11.7647 |
| geometric_mean_slowdown | 1.02371 | 1.18098 | +0.157278 |
| exact_winner_rate_percent | 76.4706 | 58.8235 | -17.6471 |
| oracle_capture_percent | 76.4706 | 58.8235 | -17.6471 |
| catastrophic_rate_over_10_percent | 5.88235 | 23.5294 | +17.6471 |

Training groups: 100; minimum required: 100.
Promotion requires lower mean regret and no increase in >20% catastrophic decisions.
The offline runtime baseline never controls production decisions.

A versioned model artifact is written to `smartparallel_utility_model.spm`.
Shadow-only artifacts may be inspected and loaded for testing, but production use still requires promotion.
