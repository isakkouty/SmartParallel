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
    parser.add_argument("kind", choices=("v1.7", "v1.6"))
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
        required = V17_REQUIRED if args.kind == "v1.7" else V16_REQUIRED
        missing_fields = sorted(required - set(reader.fieldnames))
        if missing_fields:
            raise ValueError(f"benchmark CSV is missing columns: {missing_fields}")
        rows = list(reader)

    if not rows:
        raise ValueError("benchmark CSV contains no evidence rows")

    repetitions = {int(row["repetition"]) for row in rows}
    if len(repetitions) < args.minimum_repetitions:
        raise ValueError(
            f"benchmark CSV contains {len(repetitions)} distinct repetitions; "
            f"expected at least {args.minimum_repetitions}"
        )

    if args.kind == "v1.7":
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
