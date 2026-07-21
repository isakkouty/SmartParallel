#!/usr/bin/env python3
"""Validate and compare nested benchmark outputs across CPU backends."""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

BACKENDS = {
    "thread_pool": "v1.1.0_nested_execution_optimized.csv",
    "static_thread": "v1.1.0_nested_execution_optimized_static_run.csv",
    "one_tbb": "v1.1.0_nested_execution_optimized_tbb_run.csv",
}
TRACE_FILES = {
    "thread_pool": "v1.1.0_nested_execution_optimized_trace_run_trace.csv",
    "static_thread": "v1.1.0_nested_execution_optimized_static_trace_run_trace.csv",
    "one_tbb": "v1.1.0_nested_execution_optimized_tbb_trace_run_trace.csv",
}


def load(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        raise RuntimeError(f"missing validation output: {path}")
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def key(row: dict[str, str]) -> tuple[str, str, str]:
    return row["suite"], row["case"], row["configuration"]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_directory", nargs="?", default="validation/output")
    args = parser.parse_args()
    output_dir = Path(args.output_directory)

    summaries: dict[str, dict[tuple[str, str, str], dict[str, str]]] = {}
    for backend, filename in BACKENDS.items():
        rows = load(output_dir / filename)
        require(rows, f"empty summary for {backend}")
        require(all(row.get("engine") == backend for row in rows),
                f"{backend} summary contains a different engine")
        require(all(row.get("correct") == "1" and row.get("checksum") == row.get("expected_checksum")
                    for row in rows),
                f"{backend} summary contains a checksum failure")
        summaries[backend] = {key(row): row for row in rows}

    baseline_keys = set(summaries["thread_pool"])
    for backend, rows in summaries.items():
        require(set(rows) == baseline_keys,
                f"{backend} summary does not contain the same workload/configuration set")

    for item in baseline_keys:
        expected = summaries["thread_pool"][item]["expected_checksum"]
        for backend, rows in summaries.items():
            require(rows[item]["expected_checksum"] == expected,
                    f"expected checksum differs for {item} under {backend}")

    for backend, filename in TRACE_FILES.items():
        rows = load(output_dir / filename)
        parallel = [row for row in rows if row.get("parallel") == "1"]
        require(parallel, f"{backend} trace contains no parallel records")
        require(all(row.get("backend_confirmed") == "1" and row.get("backend") == backend
                    for row in parallel),
                f"{backend} trace does not prove the requested backend executed")
        require(all(int(row.get("max_root_leased_workers") or 0) <= 4 for row in rows),
                f"{backend} trace exceeded the four-participant validation budget")
        if backend != "thread_pool":
            require(all(int(row.get("helpers_submitted") or 0) == 0 for row in rows),
                    f"{backend} trace leaked ThreadPool dependency helpers")

    comparison_path = output_dir / "v1.1.0_nested_execution_cross_backend_comparison.csv"
    with comparison_path.open("w", newline="", encoding="utf-8") as handle:
        fieldnames = [
            "suite", "case", "configuration", "parallel_levels", "dimensions",
            "thread_pool_median_ms", "static_thread_median_ms", "one_tbb_median_ms",
            "checksum",
        ]
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for item in sorted(baseline_keys):
            base = summaries["thread_pool"][item]
            writer.writerow({
                "suite": item[0],
                "case": item[1],
                "configuration": item[2],
                "parallel_levels": base["parallel_levels"],
                "dimensions": base["dimensions"],
                "thread_pool_median_ms": base["median_ms"],
                "static_thread_median_ms": summaries["static_thread"][item]["median_ms"],
                "one_tbb_median_ms": summaries["one_tbb"][item]["median_ms"],
                "checksum": base["checksum"],
            })

    print("Cross-backend validation: PASS")
    print(f"Comparison written: {comparison_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # validation tool: report a concise release-gate error
        print(f"Cross-backend validation: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
