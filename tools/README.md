# Analysis tools

## `plot_benchmark.py`

Creates documentation-ready plots from benchmark CSV files. Run with `--help` for supported x/y selection and output options.

## `phase1_dataset_audit.py`

Audits calibration and holdout candidate datasets for schema completeness, production-safe feature availability, and learning readiness.

## `phase1_regret_ranker.py`

Trains and evaluates the Phase 1 regret-aware utility ranker and writes model, metrics, comparisons, and holdout decisions.

Python cache directories are generated locally and must not be committed.
