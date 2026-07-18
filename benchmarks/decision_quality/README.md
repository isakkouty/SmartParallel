# Decision-quality audit

This audit answers the counterfactual question missing from the original benchmarks.
For every case it measures three independent paths:

1. forced sequential;
2. forced oneTBB using `oneapi::tbb::parallel_for`;
3. normal adaptive `smart::parallel_for`.

The combined CSV contains raw timings, speedups, the measured best backend, the
adaptive selection, decision correctness, regret, numerical checksums, maximum
error, cache/fast-path state, profile estimates, and detailed scheduler timing.

Run:

```bat
benchmarks\decision_quality\scripts\run_decision_quality_audit.bat
```

Output:

```text
validation\output\all_benchmarks_decision_quality.csv
```

`decision_correct` is true when SmartParallel chooses the measured best backend,
or when total adaptive regret is no more than 10%, treating near-ties as harmless.
`adaptive_regret = adaptive_ms / min(sequential_ms, forced_onetbb_ms)`.
