#!/usr/bin/env python3
"""Validate benchmark smoke evidence without applying publication performance gates."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path


V17_REQUIRED = {
    "schema_version",
    "benchmark",
    "variant",
    "repetition",
    "value",
    "unit",
}

V18_REQUIRED = {
    "schema_version",
    "smartparallel_version",
    "benchmark",
    "variant",
    "metric",
    "repetition_index",
    "value",
    "unit",
    "effective_cpu_capacity",
    "declared_governor_budget",
    "requested_workers",
    "minimum_workers",
    "preferred_workers",
    "maximum_workers",
    "granted_workers",
    "scheduler_cap",
    "observed_participating_workers",
    "admission_result",
    "correctness",
    "peak_participation",
}


V16_REQUIRED = {
    "schema_version",
    "benchmark_version",
    "smartparallel_version",
    "operation",
    "phase",
    "repetition",
    "duration_ms",
    "execution_valid",
    "reference_accuracy_pass",
    "reproducibility_pass",
    "route_authentication_pass",
    "numerical_capability_pass",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate that a benchmark smoke run produced structurally valid, "
            "finite, correctness-authenticated evidence. This intentionally does "
            "not enforce publication performance objectives."
        )
    )
    parser.add_argument("kind", choices=("v1.8", "v1.7", "v1.6"))
    parser.add_argument("raw_csv", type=Path)
    parser.add_argument("--minimum-repetitions", type=int, default=1)
    return parser.parse_args()


def require_finite(value: str, field: str, line: int) -> None:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise ValueError(f"line {line}: {field} is not numeric: {value!r}") from exc
    if not math.isfinite(parsed):
        raise ValueError(f"line {line}: {field} is not finite: {value!r}")
    if parsed < 0.0:
        raise ValueError(f"line {line}: {field} is negative: {value!r}")



def validate_v18(rows: list[dict[str, str]]) -> None:
    variants: set[tuple[str, str, str]] = set()
    integer_fields = (
        "effective_cpu_capacity",
        "declared_governor_budget",
        "runtime_ceiling",
        "requested_workers",
        "minimum_workers",
        "preferred_workers",
        "maximum_workers",
        "granted_workers",
        "scheduler_cap",
        "observed_participating_workers",
        "correctness",
        "peak_participation",
        "bounded_bypass_count",
    )
    for line, row in enumerate(rows, start=2):
        require_finite(row["value"], "value", line)
        require_finite(row["duration_ms"], "duration_ms", line)
        require_finite(row["wait_duration_us"], "wait_duration_us", line)
        try:
            repetition = int(row["repetition_index"])
        except ValueError as exc:
            raise ValueError(
                f"line {line}: repetition_index is not an integer: "
                f"{row['repetition_index']!r}"
            ) from exc
        if repetition < 0:
            raise ValueError(f"line {line}: repetition_index is negative")
        if row["schema_version"] != "2" or row["smartparallel_version"] != "1.8.0":
            raise ValueError(
                f"line {line}: unsupported v1.8 evidence identity "
                f"{row['schema_version']!r}/{row['smartparallel_version']!r}"
            )
        for field in integer_fields:
            try:
                value = int(row[field])
            except ValueError as exc:
                raise ValueError(
                    f"line {line}: {field} is not an integer: {row[field]!r}"
                ) from exc
            if value < 0:
                raise ValueError(f"line {line}: {field} is negative")
        if row["correctness"] != "1":
            raise ValueError(f"line {line}: correctness was not authenticated")
        if not row["benchmark"] or not row["variant"] or not row["metric"] or not row["unit"]:
            raise ValueError(f"line {line}: benchmark identity fields must be non-empty")
        if int(row["minimum_workers"]) > int(row["preferred_workers"]):
            raise ValueError(f"line {line}: minimum workers exceed preferred workers")
        if int(row["preferred_workers"]) > int(row["maximum_workers"]):
            raise ValueError(f"line {line}: preferred workers exceed maximum workers")
        if int(row["granted_workers"]) > int(row["maximum_workers"]):
            raise ValueError(f"line {line}: grant exceeds maximum workers")
        variants.add((row["benchmark"], row["variant"], row["metric"]))

    required_variants = {
        ("governor_overhead", "uncontended_exact_acquire_release", "latency"),
        ("governor_overhead", "direct_cancellation_notification", "latency"),
        ("admission_fairness", "large_exact_request", "completion_rank"),
        ("governed_vs_ungoverned", "governed", "throughput"),
        ("governed_vs_ungoverned", "ungoverned", "throughput"),
        ("governed_vs_ungoverned", "governed", "peak_participation"),
        ("governed_vs_ungoverned", "ungoverned", "peak_participation"),
        ("adaptive_partial_grant", "available_one_of_preferred_budget", "granted_workers"),
        ("nested_execution", "depth_4", "peak_participation"),
        ("deterministic_exact_grant", "success", "accepted"),
        ("deterministic_exact_grant", "insufficient_budget_failure", "accepted"),
        ("scheduler_comparison", "thread_pool", "duration"),
        ("scheduler_comparison", "static_thread", "duration"),
    }
    missing = sorted(required_variants - variants)
    if missing:
        raise ValueError(f"missing required v1.8 benchmark variants: {missing}")


def validate_v17(rows: list[dict[str, str]]) -> None:
    variants: set[tuple[str, str]] = set()
    for line, row in enumerate(rows, start=2):
        require_finite(row["value"], "value", line)
        try:
            repetition = int(row["repetition"])
        except ValueError as exc:
            raise ValueError(
                f"line {line}: repetition is not an integer: {row['repetition']!r}"
            ) from exc
        if repetition < 0:
            raise ValueError(f"line {line}: repetition is negative")
        if row["schema_version"] != "1":
            raise ValueError(
                f"line {line}: unsupported v1.7 schema {row['schema_version']!r}"
            )
        if not row["benchmark"] or not row["variant"] or not row["unit"]:
            raise ValueError(f"line {line}: benchmark identity fields must be non-empty")
        variants.add((row["benchmark"], row["variant"]))

    required_variants = {
        ("api_overhead", "free_function"),
        ("api_overhead", "explicit_runtime"),
        ("api_overhead", "copied_context"),
        ("startup", "adaptive_cold"),
        ("startup", "adaptive_warm"),
        ("startup", "deterministic"),
    }
    missing = sorted(required_variants - variants)
    if missing:
        raise ValueError(f"missing required v1.7 benchmark variants: {missing}")


def validate_v16(rows: list[dict[str, str]]) -> None:
    operations: set[str] = set()
    for line, row in enumerate(rows, start=2):
        require_finite(row["duration_ms"], "duration_ms", line)
        try:
            repetition = int(row["repetition"])
        except ValueError as exc:
            raise ValueError(
                f"line {line}: repetition is not an integer: {row['repetition']!r}"
            ) from exc
        if repetition < 0:
            raise ValueError(f"line {line}: repetition is negative")
        if row["schema_version"] != "2":
            raise ValueError(
                f"line {line}: unsupported v1.6 schema {row['schema_version']!r}"
            )
        for field in (
            "execution_valid",
            "reproducibility_pass",
            "route_authentication_pass",
            "numerical_capability_pass",
        ):
            if row[field] != "1":
                raise ValueError(
                    f"line {line}: correctness/authentication field {field}={row[field]!r}"
                )
        # Fast and Reproducible adversarial reductions intentionally expose
        # their weaker accuracy contract. Accurate rows must meet the
        # independent reference-accuracy gate.
        if row.get("numerical_policy") == "Accurate" and row["reference_accuracy_pass"] != "1":
            raise ValueError(
                f"line {line}: Accurate evidence failed reference accuracy"
            )
        operations.add(row["operation"])

    required_operations = {"axpy", "dot", "norm", "stencil_2d", "heat_diffusion_20"}
    missing = sorted(required_operations - operations)
    if missing:
        raise ValueError(f"missing required v1.6 operations: {missing}")


def main() -> int:
    args = parse_args()
    if args.minimum_repetitions <= 0:
        raise ValueError("--minimum-repetitions must be positive")
    if not args.raw_csv.is_file():
        raise FileNotFoundError(f"benchmark raw CSV does not exist: {args.raw_csv}")

    with args.raw_csv.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None:
            raise ValueError("benchmark CSV has no header")
        required = (
            V18_REQUIRED
            if args.kind == "v1.8"
            else V17_REQUIRED
            if args.kind == "v1.7"
            else V16_REQUIRED
        )
        missing_fields = sorted(required - set(reader.fieldnames))
        if missing_fields:
            raise ValueError(f"benchmark CSV is missing columns: {missing_fields}")
        rows = list(reader)

    if not rows:
        raise ValueError("benchmark CSV contains no evidence rows")

    repetition_field = "repetition_index" if args.kind == "v1.8" else "repetition"
    repetitions = {int(row[repetition_field]) for row in rows}
    if len(repetitions) < args.minimum_repetitions:
        raise ValueError(
            f"benchmark CSV contains {len(repetitions)} distinct repetitions; "
            f"expected at least {args.minimum_repetitions}"
        )

    if args.kind == "v1.8":
        validate_v18(rows)
    elif args.kind == "v1.7":
        validate_v17(rows)
    else:
        validate_v16(rows)

    print(
        f"Benchmark smoke validation: PASS ({args.kind}, {len(rows)} rows, "
        f"{len(repetitions)} repetitions)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # concise CLI diagnostics
        print(f"Benchmark smoke validation: FAIL: {exc}")
        raise SystemExit(1)
