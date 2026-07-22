# Analysis tools

## `plot_real_world_results.py`

Validates the final v1.1 real-world CSV schemas, correctness flags, backend authentication, and four-participant budget, then generates the release PNG/SVG figures and aggregate metrics.

From the repository root:

```text
python -m pip install pandas matplotlib
python tools/plot_real_world_results.py
```

Default input:

```text
validation/output/real_world/
```

Default output:

```text
docs/v1.1/assets/benchmarks/
```

Use `--input-dir` and `--output-dir` for custom locations.

## `check_documentation.py`

Validates local Markdown links, rejects malformed control characters, and checks that v1.0/v1.1 pages carry the appropriate archive/current markers.

```text
python tools/check_documentation.py
```

## Historical analysis tools

- `plot_benchmark.py` — generic plot utility used by earlier benchmark workflows.
- `phase1_dataset_audit.py` — audits the historical Phase 1 calibration/holdout datasets.
- `phase1_regret_ranker.py` — trains and evaluates the historical regret-aware utility ranker.

Python cache directories are local artifacts and must not be committed.
