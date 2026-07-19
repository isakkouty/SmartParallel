#!/usr/bin/env python3
"""Create documentation-ready charts from SmartParallel benchmark CSV files."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path
from typing import Iterable


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Plot timing or speedup columns from a SmartParallel benchmark CSV."
    )
    parser.add_argument("csv_file", type=Path, help="Benchmark CSV to read.")
    parser.add_argument("--x", help="Column used for x-axis labels.")
    parser.add_argument(
        "--y",
        nargs="+",
        help="Numeric columns to plot. Defaults to detected timing columns, then speedup columns.",
    )
    parser.add_argument("--title", help="Chart title. Defaults to the CSV filename.")
    parser.add_argument(
        "--ylabel", help="Y-axis label inferred from selected columns when omitted."
    )
    parser.add_argument("--output", type=Path, help="Output PNG path.")
    parser.add_argument(
        "--dpi",
        type=int,
        default=180,
        help="PNG resolution in dots per inch (default: 180).",
    )
    return parser.parse_args()


def load_rows(csv_file: Path) -> tuple[list[str], list[dict[str, str]]]:
    if not csv_file.is_file():
        raise FileNotFoundError(f"CSV file does not exist: {csv_file}")

    with csv_file.open("r", encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames is None:
            raise ValueError(f"CSV has no header: {csv_file}")
        rows = list(reader)

    if not rows:
        raise ValueError(f"CSV contains no data rows: {csv_file}")

    return list(reader.fieldnames), rows


def is_numeric_column(rows: Iterable[dict[str, str]], column: str) -> bool:
    found_value = False
    for row in rows:
        value = row.get(column, "").strip()
        if not value:
            continue
        try:
            number = float(value)
        except ValueError:
            return False
        if not math.isfinite(number):
            return False
        found_value = True
    return found_value


def detect_x_column(headers: list[str]) -> str:
    for candidate in ("case", "benchmark", "iterations", "work_items", "metric"):
        if candidate in headers:
            return candidate
    return headers[0]


def detect_y_columns(headers: list[str], rows: list[dict[str, str]]) -> list[str]:
    numeric_headers = [header for header in headers if is_numeric_column(rows, header)]
    timing = [header for header in numeric_headers if header.endswith("_ms")]
    preferred_timing = [
        header
        for header in timing
        if any(
            token in header
            for token in ("sequential", "smartparallel", "adaptive", "forced_onetbb", "total")
        )
    ]
    if preferred_timing:
        return preferred_timing[:5]
    if timing:
        return timing[:5]

    speedups = [header for header in numeric_headers if "speedup" in header or "regret" in header]
    if speedups:
        return speedups[:5]

    return numeric_headers[:3]


def validate_columns(
    headers: list[str],
    rows: list[dict[str, str]],
    x_column: str,
    y_columns: list[str],
) -> None:
    missing = [column for column in [x_column, *y_columns] if column not in headers]
    if missing:
        raise ValueError(f"Columns not found in CSV: {', '.join(missing)}")

    non_numeric = [column for column in y_columns if not is_numeric_column(rows, column)]
    if non_numeric:
        raise ValueError(f"Y-axis columns are not numeric: {', '.join(non_numeric)}")


def default_output_path(csv_file: Path) -> Path:
    return csv_file.parent / "plots" / f"{csv_file.stem}.png"


def infer_y_label(y_columns: list[str]) -> str:
    if all(column.endswith("_ms") for column in y_columns):
        return "Time (ms)"
    if all("speedup" in column for column in y_columns):
        return "Speedup (x)"
    if all("regret" in column for column in y_columns):
        return "Regret (x)"
    return "Value"


def create_plot(
    rows: list[dict[str, str]],
    x_column: str,
    y_columns: list[str],
    title: str,
    y_label: str,
    output: Path,
    dpi: int,
) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError as error:
        raise RuntimeError(
            "matplotlib is required. Install it with: python -m pip install matplotlib"
        ) from error

    labels = [row[x_column] for row in rows]
    positions = list(range(len(rows)))

    figure, axis = plt.subplots(figsize=(max(8.0, len(rows) * 0.55), 5.2))
    for column in y_columns:
        values = [float(row[column]) if row[column].strip() else math.nan for row in rows]
        axis.plot(positions, values, marker="o", label=column)

    axis.set_title(title)
    axis.set_xlabel(x_column)
    axis.set_ylabel(y_label)
    axis.set_xticks(positions)
    axis.set_xticklabels(labels, rotation=35, ha="right")
    axis.grid(True, axis="y", alpha=0.3)
    axis.legend()
    figure.tight_layout()

    output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output, dpi=dpi, bbox_inches="tight")
    plt.close(figure)


def main() -> int:
    arguments = parse_arguments()
    headers, rows = load_rows(arguments.csv_file)

    x_column = arguments.x or detect_x_column(headers)
    y_columns = arguments.y or detect_y_columns(headers, rows)
    if not y_columns:
        raise ValueError("No numeric timing, speedup, regret, or value columns were detected.")

    validate_columns(headers, rows, x_column, y_columns)

    output = arguments.output or default_output_path(arguments.csv_file)
    title = arguments.title or arguments.csv_file.stem.replace("_", " ").title()
    y_label = arguments.ylabel or infer_y_label(y_columns)

    create_plot(rows, x_column, y_columns, title, y_label, output, arguments.dpi)
    print(f"Chart written to: {output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, RuntimeError, ValueError) as error:
        raise SystemExit(f"error: {error}") from error
