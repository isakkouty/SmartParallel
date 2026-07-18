#!/usr/bin/env python3
"""SmartParallel V1 Phase 1 offline decision-objective experiment.

Trains a shared linear utility model from full candidate groups using a bounded,
regret-weighted pairwise logistic loss. The learned model is deliberately isolated
from all legacy predicted-runtime fields, then compared with the legacy winner on
a separate holdout CSV.

Only Python's standard library is required.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Sequence, Tuple

EPS = 1.0e-12
BACKENDS = ("Sequential", "ThreadPool", "oneTBB", "OpenMP")
SCHEDULES = ("Sequential", "DynamicChunks", "StaticChunks", "Guided")


@dataclass
class Row:
    group: str
    suite: str
    plan: str
    runtime: float
    predicted_runtime: float
    values: Dict[str, str]
    raw_features: List[float]
    features: List[float] | None = None


@dataclass
class Scaler:
    means: List[float]
    scales: List[float]

    def transform(self, values: Sequence[float]) -> List[float]:
        return [1.0] + [
            max(-4.0, min(4.0, (value - mean) / scale))
            for value, mean, scale in zip(values, self.means, self.scales)
        ]


def number(row: Dict[str, str], key: str, default: float = 0.0) -> float:
    try:
        value = float(row.get(key, ""))
        return value if math.isfinite(value) else default
    except (TypeError, ValueError):
        return default


def parse_plan(plan: str) -> Tuple[str, str, float, float]:
    parts = plan.split("/")
    backend = parts[0] if parts else "Unknown"
    schedule = parts[1] if len(parts) > 1 else "Sequential"
    workers = 1.0
    chunk = 1.0
    for part in parts[2:]:
        if part.startswith("w"):
            try:
                workers = max(1.0, float(part[1:]))
            except ValueError:
                pass
        elif part.startswith("c"):
            try:
                chunk = max(1.0, float(part[1:]))
            except ValueError:
                pass
    return backend, schedule, workers, chunk


def make_raw_features(row: Dict[str, str], suite: str) -> List[float]:
    plan = row.get("plan", "")
    backend, schedule, workers, chunk = parse_plan(plan)
    logical_iterations = number(row, "logical_iterations", number(row, "size", 1.0))
    jobs = max(1.0, number(row, "jobs", workers))

    # Production-safe context only. No case names and no legacy prediction outputs.
    context = [
        math.log1p(max(0.0, logical_iterations)),
        number(row, "profile_available", 0.0),
        math.log1p(max(0.0, number(row, "profile_median_ms_per_iteration", 0.0)) * 1.0e6),
        number(row, "profile_coefficient_of_variation", 0.0),
        number(row, "profile_tail_ratio", 0.0),
        number(row, "profile_parallel_worthiness", 0.0),
        number(row, "profile_regional_cost_ratio", 0.0),
        number(row, "hint_available", 0.0),
        number(row, "hint_arithmetic_intensity", 0.0),
        number(row, "hint_branchiness", 0.0),
        number(row, "hint_memory_randomness", 0.0),
        number(row, "hint_vectorization_potential", 0.0),
        number(row, "hint_dependency_depth", 0.0),
        math.log1p(max(0.0, number(row, "hint_bytes_touched_per_iteration", 0.0))),
        math.log1p(max(0.0, number(row, "hint_external_working_set_bytes", 0.0))),
        number(row, "hint_feature_confidence", 0.0),
    ]

    action_continuous = [
        math.log1p(jobs),
        math.log1p(chunk),
        math.log1p(max(1.0, logical_iterations) / jobs),
        math.log1p(max(1.0, logical_iterations) / chunk),
    ]
    backend_flags = [1.0 if backend == name else 0.0 for name in BACKENDS]
    schedule_flags = [1.0 if schedule == name else 0.0 for name in SCHEDULES]
    interactions = [value * flag for flag in backend_flags for value in context]
    return context + action_continuous + backend_flags + schedule_flags + interactions


def read_rows(path: Path) -> List[Row]:
    rows: List[Row] = []
    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        for values in reader:
            runtime = number(values, "actual_ms", -1.0)
            predicted = number(values, "predicted_runtime_ms", math.inf)
            if runtime <= 0.0:
                continue
            case = values.get("case", "unknown")
            suite = values.get("suite", "calibration")
            group = f"{suite}::{case}"
            rows.append(Row(
                group=group,
                suite=suite,
                plan=values.get("plan", "unknown"),
                runtime=runtime,
                predicted_runtime=predicted,
                values=values,
                raw_features=make_raw_features(values, suite),
            ))
    if not rows:
        raise RuntimeError(f"no usable candidates found in {path}")
    return rows


def fit_scaler(rows: Sequence[Row]) -> Scaler:
    columns = list(zip(*(row.raw_features for row in rows)))
    means = [statistics.fmean(column) for column in columns]
    scales = []
    for column, mean in zip(columns, means):
        variance = statistics.fmean((value - mean) ** 2 for value in column)
        scales.append(max(math.sqrt(variance), 1.0e-6))
    return Scaler(means, scales)


def apply_scaler(rows: Sequence[Row], scaler: Scaler) -> None:
    for row in rows:
        row.features = scaler.transform(row.raw_features)


def grouped(rows: Sequence[Row]) -> Dict[str, List[Row]]:
    result: Dict[str, List[Row]] = {}
    for row in rows:
        result.setdefault(row.group, []).append(row)
    return result


def sigmoid(value: float) -> float:
    if value >= 0.0:
        e = math.exp(-value)
        return 1.0 / (1.0 + e)
    e = math.exp(value)
    return e / (1.0 + e)


def dot(weights: Sequence[float], features: Sequence[float]) -> float:
    return sum(w * x for w, x in zip(weights, features))


def build_pairs(rows: Sequence[Row], near_tie: float, max_weight: float) -> List[Tuple[List[float], List[float], float]]:
    pairs: List[Tuple[List[float], List[float], float]] = []
    for candidates in grouped(rows).values():
        for i in range(len(candidates)):
            for j in range(i + 1, len(candidates)):
                left, right = candidates[i], candidates[j]
                better, worse = (left, right) if left.runtime <= right.runtime else (right, left)
                ratio = worse.runtime / max(better.runtime, EPS)
                if ratio <= 1.0 + near_tie:
                    continue
                weight = min(max_weight, abs(math.log(ratio)))
                assert better.features is not None and worse.features is not None
                pairs.append((better.features, worse.features, weight))
    return pairs


def train(rows: Sequence[Row], epochs: int, learning_rate: float, l2: float,
          near_tie: float, max_weight: float, seed: int) -> Tuple[List[float], List[float]]:
    """Train with deterministic full-batch gradients.

    The former per-pair SGD update was order-sensitive and produced extreme
    weights from only twelve calibration groups. Full-batch updates, feature
    clipping in Scaler.transform, and gradient-norm clipping keep the shadow
    experiment numerically bounded.
    """
    del seed  # deterministic by design
    pairs = build_pairs(rows, near_tie, max_weight)
    if not pairs:
        raise RuntimeError("training data produced no non-tie pairs")
    feature_count = len(pairs[0][0])
    weights = [0.0] * feature_count
    history: List[float] = []

    for _ in range(epochs):
        gradient = [0.0] * feature_count
        total_loss = 0.0
        for better, worse, importance in pairs:
            difference = [a - b for a, b in zip(better, worse)]
            z = dot(weights, difference)
            total_loss += importance * (z if z > 30.0 else math.log1p(math.exp(z)))
            probability = sigmoid(z)
            for index, delta in enumerate(difference):
                gradient[index] += importance * probability * delta

        pair_count = float(len(pairs))
        for index in range(feature_count):
            gradient[index] = gradient[index] / pair_count + l2 * weights[index]

        norm = math.sqrt(sum(value * value for value in gradient))
        if norm > 5.0:
            scale = 5.0 / norm
            gradient = [value * scale for value in gradient]

        for index in range(feature_count):
            weights[index] -= learning_rate * gradient[index]

        history.append(total_loss / len(pairs))
        learning_rate *= 0.99
    return weights, history


def percentile(values: Sequence[float], probability: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    position = probability * (len(ordered) - 1)
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def evaluate(rows: Sequence[Row], weights: Sequence[float], mode: str) -> Tuple[Dict[str, float], List[Dict[str, object]]]:
    regrets: List[float] = []
    slowdowns: List[float] = []
    details: List[Dict[str, object]] = []
    exact = 0

    for group_name, candidates in sorted(grouped(rows).items()):
        best = min(candidates, key=lambda row: row.runtime)
        if mode == "legacy":
            legacy_candidates = [row for row in candidates if math.isfinite(row.predicted_runtime) and row.predicted_runtime > 0.0]
            selected = min(legacy_candidates, key=lambda row: row.predicted_runtime) if legacy_candidates else candidates[0]
        else:
            selected = min(candidates, key=lambda row: dot(weights, row.features or []))
        regret = max(0.0, (selected.runtime - best.runtime) / best.runtime)
        slowdown = selected.runtime / best.runtime
        regrets.append(regret)
        slowdowns.append(slowdown)
        exact += int(selected.plan == best.plan)
        details.append({
            "group": group_name,
            "best_plan": best.plan,
            "selected_plan": selected.plan,
            "best_ms": best.runtime,
            "selected_ms": selected.runtime,
            "regret_percent": regret * 100.0,
            "slowdown": slowdown,
        })

    geometric_slowdown = math.exp(statistics.fmean(math.log(max(value, EPS)) for value in slowdowns))
    metrics = {
        "groups": len(regrets),
        "mean_regret_percent": statistics.fmean(regrets) * 100.0,
        "median_regret_percent": statistics.median(regrets) * 100.0,
        "p90_regret_percent": percentile(regrets, 0.90) * 100.0,
        "p95_regret_percent": percentile(regrets, 0.95) * 100.0,
        "p99_regret_percent": percentile(regrets, 0.99) * 100.0,
        "worst_regret_percent": max(regrets) * 100.0,
        "catastrophic_rate_over_20_percent": sum(value > 0.20 for value in regrets) / len(regrets) * 100.0,
        "geometric_mean_slowdown": geometric_slowdown,
        "exact_winner_rate_percent": exact / len(regrets) * 100.0,
        "oracle_capture_percent": exact / len(regrets) * 100.0,
        "catastrophic_rate_over_10_percent": sum(value > 0.10 for value in regrets) / len(regrets) * 100.0,
    }
    return metrics, details


def write_insufficient_outputs(output: Path, args: argparse.Namespace,
                               legacy: Dict[str, float], training_groups: int,
                               holdout_groups: int) -> None:
    """Write a readiness report without fitting an invalid small-sample model."""
    output.mkdir(parents=True, exist_ok=True)
    status = "INSUFFICIENT DATA — MODEL NOT TRAINED"
    payload = {
        "experiment": "SmartParallel V1 utility-learning readiness gate",
        "configuration": vars(args),
        "offline_runtime_baseline": legacy,
        "utility_model": None,
        "promotion": {
            "eligible": False,
            "status": status,
            "rule": "train only after min_training_groups independent calibration workloads exist",
            "training_groups": training_groups,
            "holdout_groups": holdout_groups,
            "minimum_training_groups": args.min_training_groups,
            "sufficient_data": False,
            "production_runtime_prediction_used": False,
        },
    }
    (output / "phase1_metrics.json").write_text(json.dumps(payload, indent=2), encoding="utf-8")

    with (output / "phase1_comparison.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["metric", "offline_runtime_baseline", "utility_model"])
        for key, value in legacy.items():
            writer.writerow([key, value, "NOT_TRAINED"])

    report = [
        "# SmartParallel V1 Phase 1 Result",
        "",
        f"**Utility-model status: {status}**",
        "",
        f"Independent calibration workloads: {training_groups}",
        f"Minimum required before training: {args.min_training_groups}",
        f"Independent holdout workloads: {holdout_groups}",
        "",
        "The utility model was intentionally not fitted or scored. Reporting a holdout score from an underdetermined model would be misleading.",
        "The legacy runtime argmin is retained only as an offline benchmark and does not control production decisions.",
        "",
        "## Offline baseline",
        "",
        "| Metric | Value |",
        "|---|---:|",
    ]
    for key, value in legacy.items():
        report.append(f"| {key} | {value:.6g} |")
    (output / "PHASE1_RESULT.md").write_text("\n".join(report) + "\n", encoding="utf-8")




def write_model_artifact(output: Path, scaler: Scaler, weights: Sequence[float], promoted: bool) -> Path:
    """Write a versioned, runtime-loadable SmartParallel utility-model artifact."""
    path = output / "smartparallel_utility_model.spm"
    status = "PROMOTED" if promoted else "SHADOW_ONLY"

    def vector_line(name: str, values: Sequence[float]) -> str:
        return name + " " + str(len(values)) + " " + " ".join(format(value, ".17g") for value in values)

    lines = [
        "SMARTPARALLEL_UTILITY_MODEL 1",
        "feature_schema phase1_utility_v1",
        f"promotion_status {status}",
        "hardware_fingerprint unspecified",
        vector_line("scaler_means", scaler.means),
        vector_line("scaler_scales", scaler.scales),
        vector_line("weights", weights),
        "END",
    ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def write_outputs(output: Path, args: argparse.Namespace, scaler: Scaler, weights: Sequence[float],
                  losses: Sequence[float], legacy: Dict[str, float], ranker: Dict[str, float],
                  details: Sequence[Dict[str, object]], training_groups: int) -> None:
    output.mkdir(parents=True, exist_ok=True)
    payload = {
        "experiment": "SmartParallel V1 regret-weighted pairwise utility ranking",
        "configuration": vars(args),
        "offline_runtime_baseline": legacy,
        "utility_model": ranker,
        "difference_utility_minus_baseline": {
            key: ranker[key] - legacy[key]
            for key in ranker if isinstance(ranker[key], (int, float)) and key in legacy
        },
        "training": {
            "initial_loss": losses[0],
            "final_loss": losses[-1],
            "weights": list(weights),
            "scaler_means": scaler.means,
            "scaler_scales": scaler.scales,
        },
    }
    with (output / "phase1_holdout_decisions.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(details[0].keys()))
        writer.writeheader()
        writer.writerows(details)

    metric_names = [key for key in legacy if key != "groups"]
    with (output / "phase1_comparison.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["metric", "offline_runtime_baseline", "utility_model", "difference"])
        for key in metric_names:
            writer.writerow([key, legacy[key], ranker[key], ranker[key] - legacy[key]])

    promoted = (
        ranker["mean_regret_percent"] < legacy["mean_regret_percent"] and
        ranker["catastrophic_rate_over_20_percent"] <= legacy["catastrophic_rate_over_20_percent"]
    )
    status = "PROMOTED" if promoted else "SHADOW ONLY — NOT PROMOTED"
    model_path = write_model_artifact(output, scaler, weights, promoted)
    payload["promotion"] = {
        "eligible": promoted,
        "status": status,
        "rule": "lower mean regret and no increase in >20% catastrophic decisions",
        "training_groups": training_groups,
        "minimum_training_groups": args.min_training_groups,
        "sufficient_data": True,
        "production_runtime_prediction_used": False,
    }
    payload["model_artifact"] = {
        "path": str(model_path),
        "format_version": 1,
        "feature_schema": "phase1_utility_v1",
        "loadable_even_when_shadow_only": True,
        "production_use_requires_promotion": True,
    }
    (output / "phase1_metrics.json").write_text(json.dumps(payload, indent=2), encoding="utf-8")
    report = [
        "# SmartParallel V1 Phase 1 Result",
        "",
        f"**Utility-model status: {status}**",
        "",
        "| Metric | Offline runtime baseline | Utility model | Delta |",
        "|---|---:|---:|---:|",
    ]
    for key in metric_names:
        report.append(f"| {key} | {legacy[key]:.6g} | {ranker[key]:.6g} | {ranker[key] - legacy[key]:+.6g} |")
    report += [
        "",
        f"Training groups: {training_groups}; minimum required: {args.min_training_groups}.",
        "Promotion requires lower mean regret and no increase in >20% catastrophic decisions.",
        "The offline runtime baseline never controls production decisions.",
        "",
        "A versioned model artifact is written to `smartparallel_utility_model.spm`.",
        "Shadow-only artifacts may be inspected and loaded for testing, but production use still requires promotion.",
    ]
    (output / "PHASE1_RESULT.md").write_text("\n".join(report) + "\n", encoding="utf-8")

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--train", default="validation/output/prediction_candidates.csv")
    parser.add_argument("--holdout", default="validation/output/holdout_candidates.csv")
    parser.add_argument("--output", default="validation/phase1")
    parser.add_argument("--epochs", type=int, default=120)
    parser.add_argument("--learning-rate", type=float, default=0.02)
    parser.add_argument("--l2", type=float, default=1.0e-4)
    parser.add_argument("--near-tie", type=float, default=0.005)
    parser.add_argument("--max-weight", type=float, default=3.0)
    parser.add_argument("--seed", type=int, default=1337)
    parser.add_argument("--min-training-groups", type=int, default=100,
                        help="minimum independent workload groups required before promotion")
    parser.add_argument("--require-promotion", action="store_true",
                        help="return non-zero only when a CI job explicitly requires model promotion")
    args = parser.parse_args()

    train_rows = read_rows(Path(args.train))
    holdout_rows = read_rows(Path(args.holdout))
    training_groups = len(grouped(train_rows))
    holdout_groups = len(grouped(holdout_rows))

    # Baseline evaluation does not depend on learned weights.
    legacy_metrics, _ = evaluate(holdout_rows, [], "legacy")

    print("SmartParallel V1 Phase 1")
    print(f"  training groups: {training_groups}/{args.min_training_groups} minimum")
    print(f"  holdout groups: {holdout_groups}")
    print(f"  offline baseline mean regret: {legacy_metrics['mean_regret_percent']:.3f}%")

    if training_groups < args.min_training_groups:
        write_insufficient_outputs(Path(args.output), args, legacy_metrics, training_groups, holdout_groups)
        print("  utility model: NOT TRAINED — INSUFFICIENT INDEPENDENT WORKLOADS")
        print(f"  report: {Path(args.output) / 'PHASE1_RESULT.md'}")
        return 3 if args.require_promotion else 0

    scaler = fit_scaler(train_rows)
    apply_scaler(train_rows, scaler)
    apply_scaler(holdout_rows, scaler)
    weights, losses = train(train_rows, args.epochs, args.learning_rate, args.l2,
                            args.near_tie, args.max_weight, args.seed)
    ranker_metrics, ranker_details = evaluate(holdout_rows, weights, "ranker")
    write_outputs(Path(args.output), args, scaler, weights, losses,
                  legacy_metrics, ranker_metrics, ranker_details, training_groups)

    passed = (
        ranker_metrics["mean_regret_percent"] < legacy_metrics["mean_regret_percent"] and
        ranker_metrics["catastrophic_rate_over_20_percent"] <= legacy_metrics["catastrophic_rate_over_20_percent"]
    )
    print(f"  utility-model mean regret: {ranker_metrics['mean_regret_percent']:.3f}%")
    print(f"  utility model: {'PROMOTED' if passed else 'SHADOW ONLY — NOT PROMOTED'}")
    print(f"  report: {Path(args.output) / 'PHASE1_RESULT.md'}")
    return 3 if args.require_promotion and not passed else 0


if __name__ == "__main__":
    raise SystemExit(main())
