#!/usr/bin/env python3
"""Audit whether Phase 1 CSVs can train a production-usable decision model."""

from __future__ import annotations
import argparse, csv, json
from pathlib import Path

REQUIRED_CONTEXT = (
    "logical_iterations",
    "profile_available",
    "profile_median_ms_per_iteration",
    "profile_coefficient_of_variation",
    "profile_tail_ratio",
    "profile_parallel_worthiness",
    "profile_regional_cost_ratio",
    "hint_arithmetic_intensity",
    "hint_branchiness",
    "hint_memory_randomness",
    "hint_vectorization_potential",
    "hint_dependency_depth",
    "hint_bytes_touched_per_iteration",
    "hint_external_working_set_bytes",
    "hint_feature_confidence",
)
FORBIDDEN_MODEL_INPUTS = (
    "predicted_runtime_ms",
    "predicted_execution_ms",
    "scheduling_overhead_ms",
    "memory_penalty_ms",
    "imbalance_penalty_ms",
    "analytical_baseline_ms",
    "hierarchical_factor",
    "hierarchical_confidence",
    "predicted_runtime_stddev_ms",
)


def columns(path: Path) -> list[str]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.reader(handle)
        return next(reader)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--train", default="validation/output/prediction_candidates.csv")
    parser.add_argument("--holdout", default="validation/output/holdout_candidates.csv")
    parser.add_argument("--output", default="validation/phase1/dataset_audit.json")
    parser.add_argument("--require-ready", action="store_true")
    args = parser.parse_args()
    train, holdout = Path(args.train), Path(args.holdout)
    train_cols, holdout_cols = columns(train), columns(holdout)
    missing_train = [c for c in REQUIRED_CONTEXT if c not in train_cols]
    missing_holdout = [c for c in REQUIRED_CONTEXT if c not in holdout_cols]
    payload = {
        "ready": not missing_train and not missing_holdout,
        "required_context_features": list(REQUIRED_CONTEXT),
        "forbidden_model_inputs": list(FORBIDDEN_MODEL_INPUTS),
        "missing_from_train": missing_train,
        "missing_from_holdout": missing_holdout,
        "note": (
            "actual_ms is permitted only as the offline label; legacy predicted-runtime "
            "fields are permitted only for the frozen baseline comparison."
        ),
    }
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print("Phase 1 dataset:", "READY" if payload["ready"] else "NOT READY")
    if missing_train:
        print("  missing train:", ", ".join(missing_train))
    if missing_holdout:
        print("  missing holdout:", ", ".join(missing_holdout))
    print("  report:", output)
    return 4 if args.require_ready and not payload["ready"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
