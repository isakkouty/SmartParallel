# v1.4 benchmark assets

These figures and generated summaries are derived from the checked-in release snapshot:

- `validation/output/v1.4.0_parallel_algorithms.csv`
- `validation/output/v1.4.0_parallel_algorithms_raw.csv`

Regenerate every asset from the repository root with:

```text
python tools/plot_v14_algorithm_results.py
```

The script validates the CSV schema, algorithm/mode coverage, checksum status, and backend-authentication fields before writing:

- PNG figures for GitHub Markdown and release pages;
- matching SVG figures for scalable reuse;
- `benchmark-metrics.json` with source SHA-256 hashes and aggregate metrics;
- `generated-results.md` with the complete automatic-result table.

The checked-in run contains seven repetitions per algorithm/mode pair. Results are machine-specific and are not universal performance guarantees.
