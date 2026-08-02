#!/usr/bin/env python3
"""Analyze SmartParallel v1.8 governed-execution evidence.

The analyzer preserves raw samples, uses paired bootstrap 95% intervals for
paired claims, reports negative and inconclusive evidence explicitly, and
produces publication SVGs plus a source-data manifest.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import random
import statistics
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Iterable

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

SCRIPT_VERSION = "2.2"
BOOTSTRAP_SEED = 1802026
BOOTSTRAP_SAMPLES = 10000


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise SystemExit(f"no benchmark records in {path}")
    required = {
        "schema_version", "benchmark", "variant", "metric", "value",
        "correctness", "repetition_index", "effective_cpu_capacity",
        "declared_governor_budget", "minimum_workers", "preferred_workers",
        "maximum_workers", "granted_workers", "peak_participation",
        "wait_duration_us", "operating_system", "compiler",
    }
    missing = sorted(required - set(rows[0]))
    if missing:
        raise SystemExit(f"raw evidence is missing columns: {', '.join(missing)}")
    return rows


def f(row: dict[str, str], name: str) -> float:
    value = row.get(name, "")
    return float(value) if value not in ("", None) else 0.0


def i(row: dict[str, str], name: str) -> int:
    return int(float(row.get(name, "0") or 0))


def percentile(values: Iterable[float], quantile: float) -> float:
    data = sorted(values)
    if not data:
        return math.nan
    if len(data) == 1:
        return data[0]
    position = (len(data) - 1) * quantile
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return data[lower]
    weight = position - lower
    return data[lower] * (1.0 - weight) + data[upper] * weight


def summary(values: list[float]) -> dict[str, float | int]:
    if not values:
        return {"sample_count": 0}
    return {
        "sample_count": len(values),
        "median": statistics.median(values),
        "mean": statistics.fmean(values),
        "p50": percentile(values, 0.50),
        "p95": percentile(values, 0.95),
        "p99": percentile(values, 0.99),
        "minimum": min(values),
        "maximum": max(values),
    }


def bootstrap_interval(values: list[float], statistic: Callable[[list[float]], float],
                       seed: int = BOOTSTRAP_SEED) -> tuple[float, float, float]:
    if not values:
        return (math.nan, math.nan, math.nan)
    point = statistic(values)
    rng = random.Random(seed)
    samples = []
    for _ in range(BOOTSTRAP_SAMPLES):
        draw = [values[rng.randrange(len(values))] for _ in values]
        samples.append(statistic(draw))
    samples.sort()
    return point, percentile(samples, 0.025), percentile(samples, 0.975)


def paired_by_repetition(rows: list[dict[str, str]], benchmark: str,
                         metric: str, first: str, second: str) -> tuple[list[float], list[float]]:
    grouped: dict[tuple[str, int], list[float]] = defaultdict(list)
    for row in rows:
        if row["benchmark"] == benchmark and row["metric"] == metric \
                and row["variant"] in (first, second):
            grouped[(row["variant"], i(row, "repetition_index"))].append(f(row, "value"))
    common = sorted({rep for variant, rep in grouped if variant == first}
                    & {rep for variant, rep in grouped if variant == second})
    return ([statistics.median(grouped[(first, rep)]) for rep in common],
            [statistics.median(grouped[(second, rep)]) for rep in common])


def paired_ratio_interval(numerators: list[float], denominators: list[float],
                          seed: int = BOOTSTRAP_SEED) -> tuple[float, float, float, int]:
    pairs = [(a, b) for a, b in zip(numerators, denominators) if b > 0.0]
    if not pairs:
        return math.nan, math.nan, math.nan, 0
    ratios = [a / b for a, b in pairs]
    point, low, high = bootstrap_interval(ratios, statistics.median, seed)
    return point, low, high, len(ratios)


def status_higher(interval: tuple[float, float, float, int], threshold: float = 1.0) -> str:
    point, low, high, _ = interval
    if math.isnan(point):
        return "INCONCLUSIVE — MORE EVIDENCE REQUIRED"
    if low > threshold:
        return "PASS"
    if high < threshold:
        return "FAIL"
    return "INCONCLUSIVE — NO MATERIAL REGRESSION DETECTED" if point >= 0.95 else \
        "INCONCLUSIVE — MORE EVIDENCE REQUIRED"


def status_lower(interval: tuple[float, float, float, int], threshold: float = 1.0) -> str:
    point, low, high, _ = interval
    if math.isnan(point):
        return "INCONCLUSIVE — MORE EVIDENCE REQUIRED"
    if high < threshold:
        return "PASS"
    if low > threshold:
        return "FAIL"
    return "INCONCLUSIVE — NO MATERIAL REGRESSION DETECTED" if point <= 1.05 else \
        "INCONCLUSIVE — MORE EVIDENCE REQUIRED"


def current_platform(rows: list[dict[str, str]]) -> str:
    first = rows[0]
    return f"{first['operating_system']} / {first['compiler']}"


def select(rows: list[dict[str, str]], *, benchmark: str | None = None,
           variant: str | None = None, metric: str | None = None) -> list[dict[str, str]]:
    return [row for row in rows
            if (benchmark is None or row["benchmark"] == benchmark)
            and (variant is None or row["variant"] == variant)
            and (metric is None or row["metric"] == metric)]


def save_figure(path: Path, title: str, platform: str) -> None:
    plt.title(title)
    plt.figtext(0.01, 0.01, f"SmartParallel v1.8 • {platform}", fontsize=8)
    plt.tight_layout(rect=(0, 0.035, 1, 1))
    plt.savefig(path, format="svg", bbox_inches="tight")
    plt.close()


def confidence_bar(ax, x: float, interval: tuple[float, float, float, int], label: str) -> None:
    point, low, high, count = interval
    ax.errorbar([x], [point], yerr=[[point - low], [high - point]], fmt="o", capsize=5,
                label=f"{label} (n={count})")


def regression_metrics(v15: Path | None, v16: Path | None, v17: Path | None) -> list[tuple[str, float, float, float]]:
    result: list[tuple[str, float, float, float]] = []
    if v15 and v15.exists():
        data = json.loads(v15.read_text(encoding="utf-8"))
        candidates = data.get("bootstrap_95_intervals", data.get("intervals", {}))
        for key in ("auto_vs_fastest_ratio", "stable_auto_dispatch_ratio", "auto_overhead_ratio"):
            if key in candidates:
                item = candidates[key]
                result.append(("v1.5 Vision", float(item.get("point", 1.0)),
                               float(item.get("lower_95", item.get("lower", 1.0))),
                               float(item.get("upper_95", item.get("upper", 1.0)))))
                break
    if v16 and v16.exists():
        data = json.loads(v16.read_text(encoding="utf-8"))
        item = data.get("fast_mode_paired_ratio_95", data.get("fast_mode_regression", {}))
        if isinstance(item, dict) and item:
            result.append(("v1.6 Fast", float(item.get("point", 1.0)),
                           float(item.get("lower_95", item.get("lower", 1.0))),
                           float(item.get("upper_95", item.get("upper", 1.0)))))
        elif "fast_mode_paired_ratio_interval_95" in data:
            interval = data["fast_mode_paired_ratio_interval_95"]
            result.append(("v1.6 Fast", float(data["fast_mode_largest_workload_ratio"]),
                           float(interval[0]), float(interval[1])))
        elif "paired_fast_ratio" in data:
            value = float(data["paired_fast_ratio"])
            result.append(("v1.6 Fast", value, value, value))
    if v17 and v17.exists():
        data = json.loads(v17.read_text(encoding="utf-8"))
        intervals = data.get("bootstrap_95_intervals", {})
        if "deterministic_vs_warm_ratio" in intervals:
            item = intervals["deterministic_vs_warm_ratio"]
            result.append(("v1.7 Deterministic/Warm", float(item["point"]),
                           float(item["lower_95"]), float(item["upper_95"])))
    return result


def generate_plots(rows: list[dict[str, str]], output: Path,
                   comparison_rows: list[dict[str, str]] | None,
                   regression: list[tuple[str, float, float, float]]) -> list[Path]:
    platform = current_platform(rows)
    plots: list[Path] = []

    def target(name: str) -> Path:
        path = output / name
        plots.append(path)
        return path

    # 1. Budget versus observed participation.
    fig, ax = plt.subplots(figsize=(8, 4.8))
    for variant, marker in (("governed", "o"), ("ungoverned", "x")):
        data = select(rows, benchmark="governed_vs_ungoverned", variant=variant,
                      metric="peak_participation")
        x = [i(row, "repetition_index") for row in data]
        y = [f(row, "value") for row in data]
        ax.scatter(x, y, marker=marker, label=f"{variant} observed")
    budget = max(i(row, "declared_governor_budget") for row in rows)
    ax.axhline(budget, linestyle="--", label=f"declared budget ({budget})")
    ax.set_xlabel("Repetition")
    ax.set_ylabel("Peak participating threads")
    ax.legend()
    save_figure(target("01_budget_vs_peak_participation.svg"),
                "Declared budget versus measured participation", platform)

    # 2. Paired throughput ratio with CI.
    governed, ungoverned = paired_by_repetition(
        rows, "governed_vs_ungoverned", "throughput", "governed", "ungoverned")
    throughput_interval = paired_ratio_interval(governed, ungoverned)
    fig, ax = plt.subplots(figsize=(6.5, 4.5))
    confidence_bar(ax, 0, throughput_interval, "governed / ungoverned")
    ax.axhline(1.0, linestyle="--")
    ax.set_xlim(-0.75, 0.75)
    ax.set_xticks([])
    ax.set_ylabel("Throughput ratio")
    ax.legend()
    save_figure(target("02_throughput_ratio_95ci.svg"),
                "Governed versus ungoverned throughput (paired 95% CI)", platform)

    # 3. Completion-latency percentiles.
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    positions = np.arange(3)
    width = 0.35
    for offset, variant in ((-width / 2, "governed"), (width / 2, "ungoverned")):
        values = [f(row, "value") for row in select(rows, benchmark="runtime_latency")
                  if row["variant"].startswith(variant + "_runtime_")]
        stats = [percentile(values, q) for q in (0.50, 0.95, 0.99)]
        ax.bar(positions + offset, stats, width, label=f"{variant} (n={len(values)})")
    ax.set_xticks(positions, ["p50", "p95", "p99"])
    ax.set_ylabel("Runtime completion latency (ms)")
    ax.legend()
    save_figure(target("03_completion_latency_percentiles.svg"),
                "Runtime completion latency distribution", platform)

    # 4. Lease wait ECDF.
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    for variant in ("governed", "ungoverned"):
        values = sorted(f(row, "wait_duration_us") for row in select(
            rows, benchmark="runtime_latency") if row["variant"].startswith(variant + "_runtime_"))
        if values:
            y = np.arange(1, len(values) + 1) / len(values)
            ax.step(values, y, where="post", label=f"{variant} (n={len(values)})")
    ax.set_xlabel("Lease wait duration (µs)")
    ax.set_ylabel("Empirical cumulative probability")
    ax.legend()
    save_figure(target("04_lease_wait_ecdf.svg"), "Lease wait-time distributions", platform)

    # 5. Multi-Runtime scaling.
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    counts, medians, lows, highs = [], [], [], []
    for count in (2, 4, 8):
        values = [f(row, "value") for row in select(rows,
            benchmark="governed_vs_ungoverned",
            variant=f"governed_{count}_runtimes", metric="throughput")]
        point, low, high = bootstrap_interval(values, statistics.median, BOOTSTRAP_SEED + count)
        counts.append(count); medians.append(point); lows.append(point - low); highs.append(high - point)
    ax.errorbar(counts, medians, yerr=[lows, highs], marker="o", capsize=4)
    ax.set_xlabel("Concurrent Runtime instances")
    ax.set_ylabel("Throughput (operations/s)")
    save_figure(target("05_multi_runtime_scaling.svg"),
                "Governed multi-Runtime scaling (median and 95% CI)", platform)

    # 6. Admission fairness wait distributions. The large request is queued
    # first; no more than the configured bounded-bypass limit may finish first.
    fairness_rows = select(rows, benchmark="admission_fairness",
                           metric="completion_rank")
    large_waits = [f(row, "wait_duration_us") for row in fairness_rows
                   if row["variant"] == "large_exact_request"]
    small_waits = [f(row, "wait_duration_us") for row in fairness_rows
                   if row["variant"].startswith("small_request_")]
    fig, ax = plt.subplots(figsize=(7.0, 4.8))
    ax.boxplot([large_waits, small_waits],
               tick_labels=["oldest large exact", "small fitting requests"],
               showfliers=True)
    ax.set_ylabel("Admission wait duration (µs)")
    save_figure(target("06_completion_fairness.svg"),
                "Admission fairness wait distributions (outliers retained)", platform)

    # 7. Oldest large-request duration and completion rank.
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    large_rows = [row for row in fairness_rows
                  if row["variant"] == "large_exact_request"]
    repetitions = [i(row, "repetition_index") for row in large_rows]
    waits = [f(row, "wait_duration_us") for row in large_rows]
    ranks = [f(row, "value") for row in large_rows]
    ax.scatter(repetitions, waits, label=f"wait duration (n={len(waits)})")
    ax.set_xlabel("Repetition")
    ax.set_ylabel("Oldest large-request wait (µs)")
    rank_axis = ax.twinx()
    rank_axis.plot(repetitions, ranks, marker="x", linestyle="none",
                   label="completion rank")
    rank_axis.set_ylabel("Completion rank (bounded bypass + 1 ≤ 5)")
    handles, labels = ax.get_legend_handles_labels()
    handles2, labels2 = rank_axis.get_legend_handles_labels()
    ax.legend(handles + handles2, labels + labels2, loc="best")
    save_figure(target("07_oldest_waiter_duration.svg"),
                "Oldest large-request admission under bounded bypass", platform)

    # 8. Governor overhead.
    fig, ax = plt.subplots(figsize=(9.0, 5.2))
    variants = ["uncontended_exact_acquire_release", "inherited_nested_lease",
                "flexible_partial_grant", "immediate_failure",
                "direct_cancellation_notification"]
    data = [[f(row, "value") for row in select(rows, benchmark="governor_overhead",
                                                variant=variant)] for variant in variants]
    ax.boxplot(data, tick_labels=["uncontended", "inherited", "partial", "fail-now", "cancel"],
               showfliers=True)
    ax.set_ylabel("Latency (µs)")
    ax.set_yscale("log")
    save_figure(target("08_governor_overhead.svg"),
                "Governor admission overhead (log scale, outliers retained)", platform)

    # 9. Adaptive partial grant fields.
    partial = select(rows, benchmark="adaptive_partial_grant")
    fig, ax = plt.subplots(figsize=(8.0, 4.8))
    labels = ["minimum", "preferred", "maximum", "granted"]
    values = [statistics.median([f(row, name + "_workers") for row in partial])
              for name in labels]
    ax.bar(labels, values)
    ax.set_ylabel("Workers")
    save_figure(target("09_adaptive_partial_grant.svg"),
                "Adaptive operation-specific partial grant", platform)

    # 10. Nested participation.
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    for depth in range(1, 5):
        data = select(rows, benchmark="nested_execution", variant=f"depth_{depth}")
        ax.scatter([depth] * len(data), [f(row, "value") for row in data], label=f"depth {depth}")
    grant = max(i(row, "granted_workers") for row in select(rows, benchmark="nested_execution"))
    ax.axhline(grant, linestyle="--", label=f"parent grant ({grant})")
    ax.set_xticks(range(1, 5))
    ax.set_xlabel("Nested depth")
    ax.set_ylabel("Peak participating threads")
    ax.legend(ncol=2)
    save_figure(target("10_nested_participation.svg"),
                "Nested participation versus parent grant", platform)

    # 11. Scheduler comparison.
    scheduler_rows = select(rows, benchmark="scheduler_comparison", metric="duration")
    schedulers = sorted({row["variant"] for row in scheduler_rows})
    fig, ax = plt.subplots(figsize=(8.0, 4.8))
    ax.boxplot([[f(row, "value") for row in scheduler_rows if row["variant"] == scheduler]
                for scheduler in schedulers], tick_labels=schedulers, showfliers=True)
    ax.set_ylabel("Duration (ms)")
    save_figure(target("11_scheduler_comparison.svg"),
                "Sequential and governed scheduler comparison", platform)

    # 12. Real machine pressure.
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    variants = ["governed", "ungoverned"]
    peak_data = [[f(row, "value") for row in select(rows,
        benchmark="governed_vs_ungoverned", variant=v, metric="peak_participation")]
        for v in variants]
    ax.boxplot(peak_data, tick_labels=variants, showfliers=True)
    effective = max(i(row, "effective_cpu_capacity") for row in rows)
    ax.axhline(effective, linestyle="--", label=f"effective capacity ({effective})")
    ax.set_ylabel("Measured peak participating threads")
    ax.legend()
    save_figure(target("12_true_machine_oversubscription.svg"),
                "Real machine-pressure participation", platform)

    # 13. Retained measured regression ratios.
    fig, ax = plt.subplots(figsize=(8.5, 4.8))
    if regression:
        x = np.arange(len(regression))
        points = [item[1] for item in regression]
        low = [item[1] - item[2] for item in regression]
        high = [item[3] - item[1] for item in regression]
        ax.errorbar(x, points, yerr=[low, high], fmt="o", capsize=5)
        ax.set_xticks(x, [item[0] for item in regression], rotation=15, ha="right")
        ax.axhline(1.0, linestyle="--")
        ax.set_ylabel("Measured ratio (95% CI)")
    else:
        ax.text(0.5, 0.5, "Retained v1.5–v1.7 metrics were not supplied to this analyzer run.",
                ha="center", va="center", transform=ax.transAxes)
        ax.set_xticks([]); ax.set_yticks([])
    save_figure(target("13_v15_v17_regression_ratios.svg"),
                "Retained measured regression ratios", platform)

    # 14. Deterministic exact grant.
    deterministic = select(rows, benchmark="deterministic_exact_grant")
    fig, ax = plt.subplots(figsize=(7.0, 4.5))
    names = ["exact grant success", "insufficient budget rejected"]
    values = [statistics.mean([f(row, "correctness") for row in deterministic
                               if row["variant"] == variant])
              for variant in ("success", "insufficient_budget_failure")]
    ax.bar(names, values)
    ax.set_ylim(0, 1.05)
    ax.set_ylabel("Correct outcomes / total")
    save_figure(target("14_deterministic_exact_grant.svg"),
                "Deterministic exact-grant success and fail-closed rejection", platform)

    # 15. Cross-platform comparable indicators are emitted only when a
    # second raw dataset is explicitly supplied. A single-platform publication
    # must not manufacture a placeholder comparison figure.
    if comparison_rows:
        fig, ax = plt.subplots(figsize=(8.0, 4.8))
        datasets = [(platform, rows), (current_platform(comparison_rows), comparison_rows)]
        labels, throughput_points, overhead_points = [], [], []
        for label, dataset in datasets:
            g, u = paired_by_repetition(dataset, "governed_vs_ungoverned", "throughput",
                                        "governed", "ungoverned")
            ratio = paired_ratio_interval(g, u)
            overhead = [f(row, "value") for row in select(dataset,
                benchmark="governor_overhead", variant="uncontended_exact_acquire_release")]
            labels.append(label)
            throughput_points.append(ratio[0])
            overhead_points.append(statistics.median(overhead) if overhead else math.nan)
        x = np.arange(len(labels))
        width = 0.35
        ax.bar(x - width/2, throughput_points, width, label="throughput ratio")
        ax.bar(x + width/2, overhead_points, width, label="lease overhead (µs)")
        ax.set_xticks(x, labels, rotation=15, ha="right")
        ax.legend()
        ax.set_ylabel("Ratio or microseconds (see legend)")
        save_figure(target("15_cross_platform_comparison.svg"),
                    "Windows versus Linux comparable indicators", platform)

    return plots


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("raw_csv", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--comparison-raw", type=Path)
    parser.add_argument("--v15-metrics", type=Path)
    parser.add_argument("--v16-metrics", type=Path)
    parser.add_argument("--v17-metrics", type=Path)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    rows = load_rows(args.raw_csv)
    comparison_rows = load_rows(args.comparison_raw) if args.comparison_raw and args.comparison_raw.exists() else None

    correctness_pass = all(row["correctness"] == "1" for row in rows)
    governed_peak_rows = select(rows, benchmark="governed_vs_ungoverned",
                                variant="governed", metric="peak_participation")
    ungoverned_peak_rows = select(rows, benchmark="governed_vs_ungoverned",
                                  variant="ungoverned", metric="peak_participation")
    governed_bound = all(f(row, "value") <= i(row, "declared_governor_budget")
                         for row in governed_peak_rows)
    true_pressure = any(f(row, "value") > i(row, "effective_cpu_capacity")
                        for row in ungoverned_peak_rows)
    partial_rows = select(rows, benchmark="adaptive_partial_grant")
    partial_pass = bool(partial_rows) and all(
        i(row, "minimum_workers") == 1
        and i(row, "preferred_workers") == i(row, "maximum_workers")
        and i(row, "granted_workers") < i(row, "preferred_workers")
        and row["correctness"] == "1" for row in partial_rows)
    nested_rows = select(rows, benchmark="nested_execution")
    nested_pass = bool(nested_rows) and all(
        i(row, "peak_participation") <= i(row, "granted_workers")
        and row["correctness"] == "1" for row in nested_rows)
    deterministic_rows = select(rows, benchmark="deterministic_exact_grant")
    deterministic_pass = bool(deterministic_rows) and all(row["correctness"] == "1"
                                                           for row in deterministic_rows)
    cancellation_rows = select(rows, benchmark="governor_overhead",
                               variant="direct_cancellation_notification")
    cancellation_pass = bool(cancellation_rows) and all(
        row["admission_result"] == "cancelled" and row["correctness"] == "1"
        for row in cancellation_rows)
    fairness_rows = select(rows, benchmark="admission_fairness",
                           metric="completion_rank")
    fairness_large_rows = [row for row in fairness_rows
                           if row["variant"] == "large_exact_request"]
    fairness_small_rows = [row for row in fairness_rows
                           if row["variant"].startswith("small_request_")]
    fairness_pass = bool(fairness_large_rows) and bool(fairness_small_rows) and all(
        row["correctness"] == "1" and row["admission_result"] == "granted"
        for row in fairness_rows) and all(
        f(row, "value") <= f(row, "bounded_bypass_count") + 1.0
        and f(row, "bounded_bypass_count") <= 4.0
        for row in fairness_large_rows)
    fairness_large_waits = [f(row, "wait_duration_us")
                            for row in fairness_large_rows]
    fairness_small_waits = [f(row, "wait_duration_us")
                            for row in fairness_small_rows]
    fairness_large_ranks = [f(row, "value") for row in fairness_large_rows]

    overhead_values = [f(row, "value") for row in select(rows,
        benchmark="governor_overhead", variant="uncontended_exact_acquire_release")]
    overhead_point, overhead_low, overhead_high = bootstrap_interval(
        overhead_values, statistics.median)
    overhead_pass = overhead_high < 10.0

    governed_throughput, ungoverned_throughput = paired_by_repetition(
        rows, "governed_vs_ungoverned", "throughput", "governed", "ungoverned")
    throughput_interval = paired_ratio_interval(governed_throughput, ungoverned_throughput)

    def per_rep_percentile(prefix: str, q: float) -> list[float]:
        grouped: dict[int, list[float]] = defaultdict(list)
        for row in select(rows, benchmark="runtime_latency"):
            if row["variant"].startswith(prefix + "_runtime_"):
                grouped[i(row, "repetition_index")].append(f(row, "value"))
        return [percentile(grouped[rep], q) for rep in sorted(grouped)]

    latency_interval = paired_ratio_interval(
        per_rep_percentile("governed", 0.95), per_rep_percentile("ungoverned", 0.95),
        BOOTSTRAP_SEED + 1)

    def fairness_per_rep(prefix: str) -> list[float]:
        grouped: dict[int, list[float]] = defaultdict(list)
        for row in select(rows, benchmark="runtime_latency"):
            if row["variant"].startswith(prefix + "_runtime_"):
                grouped[i(row, "repetition_index")].append(f(row, "value"))
        result = []
        for rep in sorted(grouped):
            values = grouped[rep]
            if values and min(values) > 0:
                result.append(max(values) / min(values))
        return result

    fairness_interval = paired_ratio_interval(
        fairness_per_rep("governed"), fairness_per_rep("ungoverned"),
        BOOTSTRAP_SEED + 2)

    regression = regression_metrics(args.v15_metrics, args.v16_metrics, args.v17_metrics)
    plots = generate_plots(rows, args.output_dir, comparison_rows, regression)

    gates = {
        "all_benchmark_records_correct": "PASS" if correctness_pass else "FAIL",
        "governor_native_participation_within_budget": "PASS" if governed_bound else "FAIL",
        "true_machine_oversubscription_observed_in_control": "PASS" if true_pressure else
            "INCONCLUSIVE — MORE EVIDENCE REQUIRED",
        "uncontended_lease_overhead_upper_95_under_10us": "PASS" if overhead_pass else "FAIL",
        "adaptive_partial_grant_contract": "PASS" if partial_pass else "FAIL",
        "nested_parent_grant_not_expanded": "PASS" if nested_pass else "FAIL",
        "deterministic_exact_grant_fail_closed": "PASS" if deterministic_pass else "FAIL",
        "direct_cancellation_notification": "PASS" if cancellation_pass else "FAIL",
        "starvation_resistant_admission_fairness": "PASS" if fairness_pass else "FAIL",
    }
    mandatory_pass = all(value == "PASS" for value in gates.values())
    performance = {
        "governed_vs_ungoverned_throughput_ratio": {
            "point": throughput_interval[0], "lower_95": throughput_interval[1],
            "upper_95": throughput_interval[2], "sample_count": throughput_interval[3],
            "status": status_higher(throughput_interval),
            "definition": "governed operations/s divided by paired ungoverned operations/s",
        },
        "governed_vs_ungoverned_p95_latency_ratio": {
            "point": latency_interval[0], "lower_95": latency_interval[1],
            "upper_95": latency_interval[2], "sample_count": latency_interval[3],
            "status": status_lower(latency_interval),
            "definition": "governed per-repetition p95 completion latency divided by ungoverned",
        },
        "governed_vs_ungoverned_completion_balance_ratio": {
            "point": fairness_interval[0], "lower_95": fairness_interval[1],
            "upper_95": fairness_interval[2], "sample_count": fairness_interval[3],
            "status": status_lower(fairness_interval),
            "definition": "governed slowest/fastest workload-completion ratio divided by ungoverned; this is a pressure-balance metric, not the queue-starvation gate",
        },
        "uncontended_acquire_release_us": {
            "point": overhead_point, "lower_95": overhead_low, "upper_95": overhead_high,
            "sample_count": len(overhead_values), "status": "PASS" if overhead_pass else "FAIL",
        },
    }

    metrics = {
        "schema_version": 3,
        "smartparallel_version": "1.8.0",
        "platform": current_platform(rows),
        "bootstrap_confidence": 0.95,
        "bootstrap_samples": BOOTSTRAP_SAMPLES,
        "bootstrap_seed": BOOTSTRAP_SEED,
        "raw_record_count": len(rows),
        "repetition_count": len({i(row, "repetition_index") for row in rows}),
        "gate_status": gates,
        "all_mandatory_benchmark_gates_pass": mandatory_pass,
        "performance_evidence": performance,
        "summaries": {
            "governed_throughput": summary(governed_throughput),
            "ungoverned_throughput": summary(ungoverned_throughput),
            "uncontended_overhead_us": summary(overhead_values),
            "governed_peak_participation": summary([f(row, "value") for row in governed_peak_rows]),
            "ungoverned_peak_participation": summary([f(row, "value") for row in ungoverned_peak_rows]),
            "admission_fairness_large_completion_rank": summary(fairness_large_ranks),
            "admission_fairness_large_wait_us": summary(fairness_large_waits),
            "admission_fairness_small_wait_us": summary(fairness_small_waits),
            "admission_fairness_max_bypasses": max(
                [f(row, "bounded_bypass_count") for row in fairness_large_rows],
                default=0.0),
        },
        "negative_or_inconclusive_results": [
            {"claim": name, **value} for name, value in performance.items()
            if value.get("status") != "PASS"
        ],
    }
    (args.output_dir / "metrics.json").write_text(
        json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    grouped_values: dict[tuple[str, str, str, str], list[float]] = defaultdict(list)
    for row in rows:
        grouped_values[(row["benchmark"], row["variant"], row["metric"], row["unit"])].append(f(row, "value"))
    with (args.output_dir / "summary.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["benchmark", "variant", "metric", "unit", "sample_count",
                         "median", "mean", "p50", "p95", "p99", "minimum", "maximum"])
        for key in sorted(grouped_values):
            values = grouped_values[key]
            stats = summary(values)
            writer.writerow([*key, stats["sample_count"], stats["median"], stats["mean"],
                             stats["p50"], stats["p95"], stats["p99"],
                             stats["minimum"], stats["maximum"]])

    report = [
        "# SmartParallel v1.8 governed-execution benchmark report", "",
        f"Platform: **{metrics['platform']}**", "",
        f"Raw records: **{len(rows)}**", "",
        f"Repetitions: **{metrics['repetition_count']}**", "",
        "Performance intervals use a paired bootstrap with 95% confidence, "
        f"{BOOTSTRAP_SAMPLES} resamples, and retained seed `{BOOTSTRAP_SEED}`.", "",
        "## Mandatory governance gates", "",
    ]
    for name, value in gates.items():
        report.append(f"- `{name}`: **{value}**")
    report.extend(["", "## Performance evidence", ""])
    for name, value in performance.items():
        report.append(
            f"- `{name}`: **{value['point']:.6g}** "
            f"[{value['lower_95']:.6g}, {value['upper_95']:.6g}], "
            f"n={value['sample_count']} — **{value['status']}**")
    report.extend(["", "## Interpretation", "",
        "Governance correctness is the primary v1.8 claim. Throughput, latency, and workload "
        "completion balance are reported exactly as measured; an inconclusive or negative "
        "performance result is not rewritten as a pass. Queue starvation resistance is a "
        "separate mandatory bounded-bypass admission gate. The throughput numerator is the "
        "number of completed Runtime "
        "operations, and the denominator is paired batch elapsed seconds.", "",
        "The ungoverned control uses separate governors and can create real participating "
        "execution above effective CPU capacity. No synthetic participation counters are used.", ""])
    if metrics["negative_or_inconclusive_results"]:
        report.extend(["## Negative or inconclusive evidence", ""])
        for item in metrics["negative_or_inconclusive_results"]:
            report.append(f"- `{item['claim']}`: **{item['status']}**")
        report.append("")
    (args.output_dir / "report.md").write_text("\n".join(report), encoding="utf-8")

    raw_hash = hashlib.sha256(args.raw_csv.read_bytes()).hexdigest()
    script_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    generated = datetime.now(timezone.utc).isoformat()
    manifest = {
        "schema_version": 1,
        "script_version": SCRIPT_VERSION,
        "script_sha256": script_hash,
        "source_data": str(args.raw_csv),
        "source_data_sha256": raw_hash,
        "generation_command": " ".join(str(part) for part in [Path(__file__).name, args.raw_csv, args.output_dir]),
        "generation_timestamp_utc": generated,
        "plots": [
            {"filename": plot.name, "source_data": str(args.raw_csv),
             "source_data_sha256": raw_hash} for plot in plots
        ],
    }
    (args.output_dir / "plot-manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    for name, value in gates.items():
        print(f"{name}: {value}")
    for name, value in performance.items():
        print(f"{name}={value['point']:.6g} [{value['lower_95']:.6g}, {value['upper_95']:.6g}] "
              f"{value['status']}")
    print(args.output_dir / "report.md")
    return 0 if mandatory_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
