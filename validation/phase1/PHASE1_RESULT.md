# SmartParallel V1 Phase 1 Result

**Utility-model status: SHADOW ONLY — NOT PROMOTED**

| Metric | Offline runtime baseline | Utility model | Delta |
|---|---:|---:|---:|
| mean_regret_percent | 4.30864 | 38.7427 | +34.4341 |
| median_regret_percent | 0 | 3.82975 | +3.82975 |
| p90_regret_percent | 14.3632 | 30.854 | +16.4909 |
| p95_regret_percent | 23.6446 | 135.978 | +112.334 |
| p99_regret_percent | 30.3765 | 460.88 | +430.503 |
| worst_regret_percent | 32.0595 | 542.105 | +510.046 |
| catastrophic_rate_over_20_percent | 11.7647 | 23.5294 | +11.7647 |
| geometric_mean_slowdown | 1.0398 | 1.1869 | +0.147102 |
| exact_winner_rate_percent | 58.8235 | 35.2941 | -23.5294 |
| oracle_capture_percent | 58.8235 | 35.2941 | -23.5294 |
| catastrophic_rate_over_10_percent | 11.7647 | 23.5294 | +11.7647 |

Training groups: 100; minimum required: 100.
Promotion requires lower mean regret and no increase in >20% catastrophic decisions.
The offline runtime baseline never controls production decisions.
