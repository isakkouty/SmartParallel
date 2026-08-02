#!/usr/bin/env python3
"""Validate SmartParallel v1.6 schema-v2 evidence and create publication assets."""
from __future__ import annotations

import csv
import json
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt

POLICIES = ["Fast", "Reproducible", "Accurate"]
SCHEMA_VERSION = "2"
REQUIRED_FIELDS = {
    "schema_version", "benchmark_version", "smartparallel_version", "compiler",
    "operating_system", "architecture", "cpu_description", "operation",
    "data_type", "workload_size", "view_layout", "stride", "phase",
    "repetition", "duration_ms", "result_bits", "result_digest", "reference",
    "reference_digest", "absolute_error", "relative_error", "execution_valid",
    "reference_accuracy_pass", "reproducibility_pass",
    "route_authentication_pass", "numerical_capability_pass", "scheduler",
    "numerical_policy", "evaluation_order", "accumulation_algorithm",
    "canonical_plan", "requested_scheduler", "worker_budget", "actual_workers",
}


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None:
            raise RuntimeError("raw benchmark CSV has no header")
        missing = REQUIRED_FIELDS.difference(reader.fieldnames)
        if missing:
            raise RuntimeError(f"raw benchmark CSV missing fields: {sorted(missing)}")
        rows = list(reader)
    if not rows:
        raise RuntimeError("raw benchmark CSV is empty")
    if any(row["schema_version"] != SCHEMA_VERSION for row in rows):
        raise RuntimeError(f"v1.6 publication requires schema {SCHEMA_VERSION}")
    if any(row["benchmark_version"] != "1.6.0" for row in rows):
        raise RuntimeError("unexpected benchmark version")
    return rows


def stable(rows: list[dict[str, str]], operation: str | None = None) -> list[dict[str, str]]:
    selected = [row for row in rows if row["phase"] == "stable"]
    return selected if operation is None else [row for row in selected if row["operation"] == operation]


def median(rows: list[dict[str, str]], field: str) -> float:
    values = [float(row[field]) for row in rows]
    return statistics.median(values) if values else math.nan


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return math.nan
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def fast_regression_statistics(rows: list[dict[str, str]]) -> dict[str, object]:
    policy_rows = stable(rows, "sum_fast_regression_policy")
    legacy_rows = stable(rows, "sum_fast_regression_legacy")
    policy = {int(row["repetition"]): float(row["duration_ms"]) for row in policy_rows}
    legacy = {int(row["repetition"]): float(row["duration_ms"]) for row in legacy_rows}
    if set(policy) != set(legacy) or len(policy) < 5:
        raise RuntimeError("Fast regression evidence requires at least five matched stable pairs")
    log_ratios = [math.log(policy[index] / legacy[index]) for index in sorted(policy)]
    center_log = statistics.median(log_ratios)
    mad_log = statistics.median(abs(value - center_log) for value in log_ratios)
    robust_sigma = 1.4826 * mad_log
    half_width_90 = 1.645 * robust_sigma / math.sqrt(len(log_ratios))
    half_width_95 = 1.960 * robust_sigma / math.sqrt(len(log_ratios))
    center = math.exp(center_log)
    lower = math.exp(center_log - half_width_90)
    upper = math.exp(center_log + half_width_90)
    lower_95 = math.exp(center_log - half_width_95)
    upper_95 = math.exp(center_log + half_width_95)
    threshold = 1.05
    gate = lower <= threshold
    status = "pass" if upper <= threshold else "not-established" if gate else "fail"
    paired_differences_us = [
        (policy[index] - legacy[index]) * 1000.0 for index in sorted(policy)
    ]
    return {
        "pairs": len(log_ratios),
        "median_ratio": center,
        "lower_90": lower,
        "upper_90": upper,
        "lower_95": lower_95,
        "upper_95": upper_95,
        "median_difference_us": statistics.median(paired_differences_us),
        "gate": gate,
        "status": status,
    }


def grouped_summary(rows: list[dict[str, str]]) -> list[dict[str, object]]:
    keys = (
        "operation", "data_type", "workload_size", "view_layout", "stride",
        "phase", "scheduler", "requested_scheduler", "numerical_policy",
        "evaluation_order", "accumulation_algorithm", "canonical_plan",
        "worker_budget", "actual_workers",
    )
    groups: dict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        groups[tuple(row[key] for key in keys)].append(row)
    result: list[dict[str, object]] = []
    for key, samples in sorted(groups.items()):
        durations = [float(row["duration_ms"]) for row in samples]
        item = dict(zip(keys, key))
        item.update(
            samples=len(samples),
            median_ms=statistics.median(durations),
            p95_ms=percentile(durations, 0.95),
            p99_ms=percentile(durations, 0.99),
            minimum_ms=min(durations),
            maximum_ms=max(durations),
            median_absolute_error=median(samples, "absolute_error"),
            median_relative_error=median(samples, "relative_error"),
            execution_valid=all(row["execution_valid"] == "1" for row in samples),
            reference_accuracy_pass=all(
                row["reference_accuracy_pass"] == "1" for row in samples),
            reproducibility_pass=all(
                row["reproducibility_pass"] == "1" for row in samples),
            route_authentication_pass=all(
                row["route_authentication_pass"] == "1" for row in samples),
            numerical_capability_pass=all(
                row["numerical_capability_pass"] == "1" for row in samples),
            unique_result_digests=len({row["result_digest"] for row in samples}),
        )
        result.append(item)
    return result


def write_summary(path: Path, summary: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(summary[0].keys()))
        writer.writeheader()
        writer.writerows(summary)


def save_plot(path: Path, title: str, xlabel: str, ylabel: str, *, log_y: bool = False) -> None:
    plt.title(title)
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    if log_y:
        plt.yscale("log")
    plt.grid(True, alpha=0.25)
    plt.tight_layout()
    plt.savefig(path, format="svg")
    plt.close()


def plot_policy_time(rows: list[dict[str, str]], output: Path) -> None:
    operations = ["sum", "dot", "norm"]
    largest = max(int(row["workload_size"]) for row in stable(rows, "sum"))
    x = list(range(len(operations)))
    width = 0.25
    plt.figure(figsize=(8, 5))
    for policy_index, policy in enumerate(POLICIES):
        values = [median([
            row for row in stable(rows, operation)
            if row["numerical_policy"] == policy
            and int(row["workload_size"]) == largest
        ], "duration_ms") for operation in operations]
        plt.bar([position + (policy_index - 1) * width for position in x],
                values, width=width, label=policy)
    plt.xticks(x, [name.capitalize() for name in operations])
    plt.legend()
    save_plot(output, f"Numerical-policy cost ({largest:,} elements)",
              "Operation", "Median stable time (ms)")


def plot_error(rows: list[dict[str, str]], output: Path) -> None:
    operations = ["sum_adversarial", "dot_adversarial"]
    x = list(range(len(operations)))
    width = 0.25
    plt.figure(figsize=(8, 5))
    for policy_index, policy in enumerate(POLICIES):
        values = [max(median([
            row for row in stable(rows, operation)
            if row["numerical_policy"] == policy
        ], "absolute_error"), 1e-30) for operation in operations]
        plt.bar([position + (policy_index - 1) * width for position in x],
                values, width=width, label=policy)
    plt.xticks(x, ["Sum", "Dot"])
    plt.legend()
    save_plot(output, "Numerical error on cancellation-sensitive data",
              "Operation", "Median absolute error", log_y=True)


def plot_scaling(rows: list[dict[str, str]], output: Path) -> None:
    subset = stable(rows, "sum_scaling")
    plt.figure(figsize=(8, 5))
    for engine in sorted({row["requested_scheduler"] for row in subset}):
        engine_rows = [row for row in subset if row["requested_scheduler"] == engine]
        budgets = sorted({int(row["worker_budget"]) for row in engine_rows})
        times = [median([row for row in engine_rows
                         if int(row["worker_budget"]) == budget], "duration_ms")
                 for budget in budgets]
        base = times[0]
        plt.plot(budgets, [base / value for value in times], marker="o", label=engine)
    plt.plot([1, 8], [1, 8], linestyle="--", linewidth=1, label="Ideal")
    plt.legend()
    save_plot(output, "Canonical reduction scaling", "Configured worker budget",
              "Speedup versus budget 1")


def plot_axpy(rows: list[dict[str, str]], output: Path) -> None:
    plt.figure(figsize=(8, 5))
    for policy in POLICIES:
        selected = [row for row in stable(rows, "axpy")
                    if row["numerical_policy"] == policy]
        sizes = sorted({int(row["workload_size"]) for row in selected})
        rates = []
        for size in sizes:
            duration = median([row for row in selected
                               if int(row["workload_size"]) == size], "duration_ms")
            rates.append((3.0 * 8.0 * size) / (duration / 1000.0) / 1e9)
        plt.plot(sizes, rates, marker="o", label=policy)
    plt.xscale("log", base=2)
    plt.legend()
    save_plot(output, "AXPY effective memory throughput", "Elements", "Effective GB/s")


def plot_dot_norm(rows: list[dict[str, str]], output: Path) -> None:
    plt.figure(figsize=(8, 5))
    for operation in ["dot", "norm"]:
        for policy in POLICIES:
            selected = [row for row in stable(rows, operation)
                        if row["numerical_policy"] == policy]
            sizes = sorted({int(row["workload_size"]) for row in selected})
            rates = []
            for size in sizes:
                duration = median([row for row in selected
                                   if int(row["workload_size"]) == size], "duration_ms")
                rates.append(size / (duration / 1000.0) / 1e6)
            plt.plot(sizes, rates, marker="o", label=f"{operation}-{policy}")
    plt.xscale("log", base=2)
    plt.legend(fontsize="small")
    save_plot(output, "Dot and norm throughput", "Elements", "Million elements/s")


def plot_stencil(rows: list[dict[str, str]], output: Path) -> None:
    plt.figure(figsize=(8, 5))
    for policy in POLICIES:
        selected = [row for row in stable(rows, "stencil_2d")
                    if row["numerical_policy"] == policy]
        sizes = sorted({int(row["workload_size"]) for row in selected})
        rates = []
        for size in sizes:
            duration = median([row for row in selected
                               if int(row["workload_size"]) == size], "duration_ms")
            rates.append(size / (duration / 1000.0) / 1e6)
        plt.plot(sizes, rates, marker="o", label=policy)
    plt.xscale("log", base=2)
    plt.legend()
    save_plot(output, "Five-point stencil throughput", "Grid cells",
              "Million cell updates/s")


def plot_heat(rows: list[dict[str, str]], output: Path) -> None:
    direct = median(stable(rows, "heat_diffusion_20_direct_sequential"), "duration_ms")
    values = []
    labels = []
    for policy in POLICIES:
        time = median([row for row in stable(rows, "heat_diffusion_20")
                       if row["numerical_policy"] == policy], "duration_ms")
        labels.append(policy)
        values.append(direct / time)
    plt.figure(figsize=(7, 5))
    plt.bar(labels, values)
    plt.axhline(1.0, linestyle="--", linewidth=1, label="Direct sequential")
    plt.legend()
    save_plot(output, "Heat-diffusion speed versus direct sequential",
              "Policy", "Speedup")


def plot_repro_matrix(rows: list[dict[str, str]], output: Path) -> None:
    operations = ["sum_scaling", "axpy_pointwise_matrix", "stencil_pointwise_matrix"]
    selected = [row for operation in operations for row in stable(rows, operation)]
    engines = sorted({row["requested_scheduler"] for row in selected})
    budgets = sorted({int(row["worker_budget"]) for row in selected})
    labels: list[str] = []
    matrix: list[list[int]] = []
    for operation in operations:
        operation_rows = stable(rows, operation)
        expected = operation_rows[0]["result_digest"] if operation_rows else ""
        for engine in engines:
            labels.append(f"{operation.replace('_matrix', '')} / {engine}")
            line = []
            for budget in budgets:
                cell = [row for row in operation_rows
                        if row["requested_scheduler"] == engine
                        and int(row["worker_budget"]) == budget]
                line.append(1 if cell and all(
                    row["reproducibility_pass"] == "1"
                    and row["result_digest"] == expected
                    and row["route_authentication_pass"] == "1"
                    for row in cell) else 0)
            matrix.append(line)
    plt.figure(figsize=(8, max(4, 0.55 * len(labels))))
    plt.imshow(matrix, vmin=0, vmax=1, aspect="auto")
    plt.xticks(range(len(budgets)), budgets)
    plt.yticks(range(len(labels)), labels)
    for y, line in enumerate(matrix):
        for x, value in enumerate(line):
            plt.text(x, y, "PASS" if value else "FAIL", ha="center", va="center")
    save_plot(output, "Bitwise reproducibility across schedulers",
              "Configured worker budget", "Operation and scheduler")


def plot_fast_regression(rows: list[dict[str, str]], output: Path) -> None:
    stats = fast_regression_statistics(rows)
    center = float(stats["median_ratio"])
    lower = float(stats["lower_90"])
    upper = float(stats["upper_90"])
    plt.figure(figsize=(7, 5))
    plt.bar(["1,048,576 elements"], [center])
    plt.errorbar([0], [center],
                 yerr=[[center - lower], [upper - center]],
                 fmt="none", capsize=6)
    plt.axhline(1.0, linestyle="--", linewidth=1, label="No change")
    plt.axhline(1.05, linestyle=":", linewidth=1, label="5% investigation line")
    plt.legend()
    save_plot(output, "Policy-aware Fast versus retained Fast overload",
              "Elements", "Paired median-time ratio (90% robust interval)")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: analyze_v16_scientific_foundations.py RAW.csv OUTPUT_DIR", file=sys.stderr)
        return 2
    raw_path = Path(sys.argv[1])
    output_dir = Path(sys.argv[2])
    output_dir.mkdir(parents=True, exist_ok=True)
    rows = read_rows(raw_path)
    summary = grouped_summary(rows)
    summary_path = output_dir / "v1.6.0_scientific_foundations.csv"
    write_summary(summary_path, summary)

    gates = {
        "execution_valid": all(row["execution_valid"] == "1" for row in rows),
        "reproducibility": all(row["reproducibility_pass"] == "1" for row in rows),
        "route_authentication": all(
            row["route_authentication_pass"] == "1" for row in rows),
        "numerical_capability": all(
            row["numerical_capability_pass"] == "1" for row in rows),
    }

    adversarial_operations = {"sum_adversarial", "dot_adversarial"}
    required_accuracy_rows = [row for row in rows
                              if row["operation"] not in adversarial_operations
                              or row["numerical_policy"] == "Accurate"]
    gates["required_reference_accuracy"] = all(
        row["reference_accuracy_pass"] == "1" for row in required_accuracy_rows)

    error_details: dict[str, dict[str, float]] = {}
    accuracy_gates: dict[str, bool] = {}
    for operation in sorted(adversarial_operations):
        errors = {policy: median([
            row for row in stable(rows, operation)
            if row["numerical_policy"] == policy
        ], "absolute_error") for policy in POLICIES}
        error_details[operation] = errors
        accurate_rows = [row for row in stable(rows, operation)
                         if row["numerical_policy"] == "Accurate"]
        accuracy_gates[operation] = (
            bool(accurate_rows)
            and errors["Accurate"] < errors["Fast"]
            and all(row["reference_accuracy_pass"] == "1" for row in accurate_rows)
        )

    matrix_operations = ["sum_scaling", "axpy_pointwise_matrix", "stencil_pointwise_matrix"]
    matrix_gates: dict[str, bool] = {}
    for operation in matrix_operations:
        samples = stable(rows, operation)
        matrix_gates[operation] = (
            bool(samples)
            and len({row["result_digest"] for row in samples}) == 1
            and all(row["reproducibility_pass"] == "1" for row in samples)
            and all(row["route_authentication_pass"] == "1" for row in samples)
            and all(row["execution_valid"] == "1" for row in samples)
            and all(row["reference_accuracy_pass"] == "1" for row in samples)
            and all(int(row["actual_workers"]) > 1
                    for row in samples if int(row["worker_budget"]) > 1)
        )

    pointwise_plan_gate = all(
        row["evaluation_order"] == "canonical-pointwise"
        and row["accumulation_algorithm"] == "fixed_pointwise_expression"
        and row["canonical_plan"] in {
            "canonical-pointwise-v1-target4096",
            "canonical-pointwise-2d-v1-target4096",
        }
        for row in rows
        if row["numerical_policy"] != "Fast"
        and row["operation"] in {
            "axpy", "axpy_strided", "axpy_pointwise_matrix", "stencil_2d",
            "stencil_2d_padded", "stencil_pointwise_matrix", "heat_diffusion_20",
        }
    )

    largest = max(int(row["workload_size"]) for row in stable(rows, "sum"))
    legacy_time = median(stable(rows, "sum_fast_regression_legacy"), "duration_ms")
    fast_time = median(stable(rows, "sum_fast_regression_policy"), "duration_ms")
    fast_stats = fast_regression_statistics(rows)
    fast_ratio = float(fast_stats["median_ratio"])
    fast_gate = bool(fast_stats["gate"])

    policy_times = {policy: median([
        row for row in stable(rows, "sum")
        if row["numerical_policy"] == policy
        and int(row["workload_size"]) == largest
    ], "duration_ms") for policy in POLICIES}
    direct_sum_time = median([row for row in stable(rows, "sum_direct_sequential")
                              if int(row["workload_size"]) == largest], "duration_ms")
    heat_times = {policy: median([
        row for row in stable(rows, "heat_diffusion_20")
        if row["numerical_policy"] == policy
    ], "duration_ms") for policy in POLICIES}
    direct_heat_time = median(stable(rows, "heat_diffusion_20_direct_sequential"), "duration_ms")

    performance_pairs = {
        "axpy": "axpy_direct_sequential",
        "dot": "dot_direct_sequential",
        "norm": "norm_direct_sequential",
        "stencil_2d": "stencil_2d_direct_sequential",
        "heat_diffusion_20": "heat_diffusion_20_direct_sequential",
    }
    scientific_kernel_speedups: dict[str, float] = {}
    scientific_kernel_times: dict[str, dict[str, float]] = {}
    for operation, direct_operation in performance_pairs.items():
        operation_rows = stable(rows, operation)
        direct_rows = stable(rows, direct_operation)
        if not operation_rows or not direct_rows:
            raise RuntimeError(f"missing performance-sanity evidence for {operation}")
        largest_workload = max(int(row["workload_size"]) for row in operation_rows)
        smart_rows = [
            row for row in operation_rows
            if row["numerical_policy"] == "Fast"
            and int(row["workload_size"]) == largest_workload
        ]
        reference_rows = [
            row for row in direct_rows
            if int(row["workload_size"]) == largest_workload
        ]
        if not smart_rows or not reference_rows:
            raise RuntimeError(f"incomplete performance-sanity evidence for {operation}")
        smart_time = median(smart_rows, "duration_ms")
        reference_time = median(reference_rows, "duration_ms")
        scientific_kernel_speedups[operation] = reference_time / smart_time
        scientific_kernel_times[operation] = {
            "Fast": smart_time,
            "DirectSequential": reference_time,
        }

    performance_sanity_minimum_speedup = 0.5
    performance_sanity_gate = all(
        speedup >= performance_sanity_minimum_speedup
        for speedup in scientific_kernel_speedups.values()
    )

    heat_schedulers = {policy: sorted({
        row["scheduler"] for row in stable(rows, "heat_diffusion_20")
        if row["numerical_policy"] == policy
    }) for policy in POLICIES}
    canonical_ids = sorted({row["canonical_plan"] for row in rows
                            if row["numerical_policy"] != "Fast"})

    plots = {
        "policy_time": "v1.6.0_policy_execution_time.svg",
        "numerical_error": "v1.6.0_numerical_error.svg",
        "canonical_scaling": "v1.6.0_canonical_scaling.svg",
        "axpy_throughput": "v1.6.0_axpy_throughput.svg",
        "dot_norm_throughput": "v1.6.0_dot_norm_throughput.svg",
        "stencil_throughput": "v1.6.0_stencil_throughput.svg",
        "heat_speed": "v1.6.0_heat_diffusion_speed.svg",
        "reproducibility_matrix": "v1.6.0_reproducibility_matrix.svg",
        "fast_regression": "v1.6.0_fast_mode_regression.svg",
    }
    plot_policy_time(rows, output_dir / plots["policy_time"])
    plot_error(rows, output_dir / plots["numerical_error"])
    plot_scaling(rows, output_dir / plots["canonical_scaling"])
    plot_axpy(rows, output_dir / plots["axpy_throughput"])
    plot_dot_norm(rows, output_dir / plots["dot_norm_throughput"])
    plot_stencil(rows, output_dir / plots["stencil_throughput"])
    plot_heat(rows, output_dir / plots["heat_speed"])
    plot_repro_matrix(rows, output_dir / plots["reproducibility_matrix"])
    plot_fast_regression(rows, output_dir / plots["fast_regression"])

    metrics = {
        "schema_version": 2,
        "raw_samples": len(rows),
        "gates": gates,
        "accuracy_gates": accuracy_gates,
        "reproducibility_matrix_gates": matrix_gates,
        "pointwise_plan_gate": pointwise_plan_gate,
        "scientific_kernel_performance_sanity_gate": performance_sanity_gate,
        "scientific_kernel_performance_sanity_minimum_speedup":
            performance_sanity_minimum_speedup,
        "scientific_kernel_speedup_vs_direct_sequential": scientific_kernel_speedups,
        "scientific_kernel_median_ms": scientific_kernel_times,
        "adversarial_errors": error_details,
        "fast_mode_largest_workload_ratio": fast_ratio,
        "fast_mode_paired_ratio_interval_90": [
            fast_stats["lower_90"], fast_stats["upper_90"]
        ],
        "fast_mode_paired_ratio_interval_95": [
            fast_stats["lower_95"], fast_stats["upper_95"]
        ],
        "fast_mode_paired_sample_count": fast_stats["pairs"],
        "fast_mode_paired_median_difference_us": fast_stats["median_difference_us"],
        "fast_mode_regression_status": fast_stats["status"],
        "fast_mode_regression_gate": fast_gate,
        "largest_sum_median_ms": {
            **policy_times,
            "DirectSequential": direct_sum_time,
            "RetainedLegacyFast": legacy_time,
        },
        "heat_diffusion_20_median_ms": {
            **heat_times,
            "DirectSequential": direct_heat_time,
        },
        "heat_diffusion_schedulers": heat_schedulers,
        "canonical_plan_ids": canonical_ids,
        "plots": plots,
        "machine_specific": True,
        "environment": {
            "compiler": rows[0]["compiler"],
            "operating_system": rows[0]["operating_system"],
            "architecture": rows[0]["architecture"],
            "cpu_description": rows[0]["cpu_description"],
        },
    }
    metrics_path = output_dir / "v1.6.0_scientific_metrics.json"
    metrics_path.write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")

    report_lines = [
        "# SmartParallel v1.6.0 Scientific Foundations — Validation Report",
        "",
        "> Performance evidence is machine-specific. Correctness fields are based on full-output validation outside timed regions.",
        "",
        f"- Raw samples: **{len(rows)}**",
        f"- Evidence schema: **{SCHEMA_VERSION}**",
        f"- Compiler: `{rows[0]['compiler']}`",
        f"- OS / architecture: `{rows[0]['operating_system']} / {rows[0]['architecture']}`",
        f"- Execution-plan IDs: `{', '.join(canonical_ids)}`",
        "",
        "## Release gates",
        "",
    ]
    for name, passed in gates.items():
        report_lines.append(f"- {name}: **{'PASS' if passed else 'FAIL'}**")
    for operation, passed in accuracy_gates.items():
        report_lines.append(
            f"- {operation} Accurate reference and improvement: **{'PASS' if passed else 'FAIL'}**")
    for operation, passed in matrix_gates.items():
        report_lines.append(
            f"- {operation} cross-scheduler bitwise matrix: **{'PASS' if passed else 'FAIL'}**")
    report_lines.extend([
        f"- Pointwise-plan authentication: **{'PASS' if pointwise_plan_gate else 'FAIL'}**",
        f"- Scientific-kernel performance sanity (all largest Fast workloads at least "
        f"{performance_sanity_minimum_speedup:.2f}x direct sequential): "
        f"**{'PASS' if performance_sanity_gate else 'FAIL'}**",
        f"- Fast-mode paired ratio: **{fast_ratio:.4f}x** "
        f"(90% robust interval {float(fast_stats['lower_90']):.4f}–"
        f"{float(fast_stats['upper_90']):.4f}x; "
        f"{str(fast_stats['status']).upper()})",
        "",
        "## Evidence semantics",
        "",
        "- `execution_valid` checks expected finite/NaN/infinity classifications.",
        "- `reference_accuracy_pass` checks the independent numerical reference.",
        "- Fast and Reproducible are allowed to expose cancellation error on the deliberate adversarial cases; Accurate must pass the reference there.",
        "- AXPY, stencil, and heat diffusion are validated over every logical output element and recorded with full-field digests.",
        "- Fast compatibility uses adjacent alternating call pairs and a median/MAD 90% interval; it fails only when the interval's lower bound exceeds the 5% investigation threshold.",
        "",
        "## Largest sum workload timing",
        "",
        "| Route or policy | Median stable time (ms) |",
        "|---|---:|",
        f"| Direct sequential reference | {direct_sum_time:.6g} |",
        f"| Retained legacy Fast overload | {legacy_time:.6g} |",
        f"| Policy-aware Fast | {policy_times['Fast']:.6g} |",
        f"| Reproducible | {policy_times['Reproducible']:.6g} |",
        f"| Accurate | {policy_times['Accurate']:.6g} |",
        "",
        "## Scientific-kernel performance sanity",
        "",
        "The sanity gate is deliberately broad: for the largest tested workload, "
        "Fast execution must remain at least 0.5x the compact direct-sequential "
        "reference. It is a regression detector, not a universal speedup claim.",
        "",
        "| Operation | SmartParallel Fast (ms) | Direct sequential (ms) | Speed vs direct |",
        "|---|---:|---:|---:|",
        *[
            f"| {operation} | {scientific_kernel_times[operation]['Fast']:.6g} | "
            f"{scientific_kernel_times[operation]['DirectSequential']:.6g} | "
            f"{scientific_kernel_speedups[operation]:.3f}x |"
            for operation in performance_pairs
        ],
        "",
        "## Heat diffusion — 20 iterations",
        "",
        "| Policy | Median stable time (ms) | Scheduler evidence | Speed vs direct sequential |",
        "|---|---:|---|---:|",
        f"| Fast | {heat_times['Fast']:.6g} | {', '.join(heat_schedulers['Fast'])} | {direct_heat_time / heat_times['Fast']:.3f}x |",
        f"| Reproducible | {heat_times['Reproducible']:.6g} | {', '.join(heat_schedulers['Reproducible'])} | {direct_heat_time / heat_times['Reproducible']:.3f}x |",
        f"| Accurate | {heat_times['Accurate']:.6g} | {', '.join(heat_schedulers['Accurate'])} | {direct_heat_time / heat_times['Accurate']:.3f}x |",
        f"| Direct sequential | {direct_heat_time:.6g} | DirectSequential | 1.000x |",
        "",
        "## Adversarial numerical error",
        "",
        "| Operation | Fast | Reproducible | Accurate |",
        "|---|---:|---:|---:|",
    ])
    for operation, errors in error_details.items():
        report_lines.append(
            f"| {operation} | {errors['Fast']:.8g} | {errors['Reproducible']:.8g} | {errors['Accurate']:.8g} |")
    report_lines.extend([
        "",
        "## Interpretation",
        "",
        "Reproducible and Accurate reductions use fixed leaves and fixed merge trees. Reproducible and Accurate pointwise operations use worker-independent fixed pointwise tiles; they preserve each element's expression order while allowing eligible schedulers to execute tiles concurrently.",
        "",
        "Accurate AXPY and stencil intentionally share the Reproducible pointwise arithmetic contract because no stronger operation-specific arithmetic method is promised for those operations.",
        "",
        "No result here establishes cross-compiler, cross-binary, cross-architecture, safety-critical, hard-real-time, or universal performance guarantees.",
    ])
    report_path = output_dir / "v1.6.0_scientific_foundations_report.md"
    report_path.write_text("\n".join(report_lines) + "\n", encoding="utf-8")

    passed = (
        all(gates.values())
        and all(accuracy_gates.values())
        and all(matrix_gates.values())
        and pointwise_plan_gate
        and performance_sanity_gate
        and fast_gate
    )
    print(f"Analyzed {len(rows)} raw samples")
    print(f"Summary: {summary_path}")
    print(f"Report: {report_path}")
    print(
        f"Fast paired ratio: {fast_ratio:.4f}x "
        f"[{float(fast_stats['lower_90']):.4f}, {float(fast_stats['upper_90']):.4f}] "
        f"({fast_stats['status']})"
    )
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
