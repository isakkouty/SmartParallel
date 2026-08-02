# SmartParallel v1.8 governed-execution benchmark report

Platform: **Linux / GCC**

Raw records: **2232**

Repetitions: **31**

Performance intervals use a paired bootstrap with 95% confidence, 10000 resamples, and retained seed `1802026`.

## Mandatory governance gates

- `all_benchmark_records_correct`: **PASS**
- `governor_native_participation_within_budget`: **PASS**
- `true_machine_oversubscription_observed_in_control`: **PASS**
- `uncontended_lease_overhead_upper_95_under_10us`: **PASS**
- `adaptive_partial_grant_contract`: **PASS**
- `nested_parent_grant_not_expanded`: **PASS**
- `deterministic_exact_grant_fail_closed`: **PASS**
- `direct_cancellation_notification`: **PASS**
- `starvation_resistant_admission_fairness`: **PASS**

## Performance evidence

- `governed_vs_ungoverned_throughput_ratio`: **0.771707** [0.677117, 0.850243], n=31 — **FAIL**
- `governed_vs_ungoverned_p95_latency_ratio`: **1.2983** [1.17206, 1.44233], n=31 — **FAIL**
- `governed_vs_ungoverned_completion_balance_ratio`: **1.69773** [1.46891, 1.73482], n=31 — **FAIL**
- `uncontended_acquire_release_us`: **0.146248** [0.145617, 0.146789], n=31 — **PASS**

## Interpretation

Governance correctness is the primary v1.8 claim. Throughput, latency, and workload completion balance are reported exactly as measured; an inconclusive or negative performance result is not rewritten as a pass. Queue starvation resistance is a separate mandatory bounded-bypass admission gate. The throughput numerator is the number of completed Runtime operations, and the denominator is paired batch elapsed seconds.

The ungoverned control uses separate governors and can create real participating execution above effective CPU capacity. No synthetic participation counters are used.

## Negative or inconclusive evidence

- `governed_vs_ungoverned_throughput_ratio`: **FAIL**
- `governed_vs_ungoverned_p95_latency_ratio`: **FAIL**
- `governed_vs_ungoverned_completion_balance_ratio`: **FAIL**
