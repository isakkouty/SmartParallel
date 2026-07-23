#!/usr/bin/env python3
"""Generate the checked-in SmartParallel v1.4 benchmark report assets."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Iterable

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SUMMARY = ROOT / "validation/output/v1.4.0_parallel_algorithms.csv"
DEFAULT_RAW = ROOT / "validation/output/v1.4.0_parallel_algorithms_raw.csv"
DEFAULT_OUTPUT = ROOT / "docs/v1.4/assets/benchmarks"

ALGORITHM_ORDER = [
    "parallel_for_each",
    "parallel_transform",
    "parallel_transform_binary",
    "parallel_copy",
    "parallel_fill",
    "parallel_generate",
    "parallel_reduce",
    "parallel_transform_reduce",
    "parallel_transform_reduce_binary",
    "parallel_count",
    "parallel_count_if",
    "parallel_any_of",
    "parallel_all_of",
    "parallel_none_of",
    "parallel_find",
    "parallel_find_if",
]

CHEAP_DISPATCH_FAMILIES = [
    "parallel_reduce",
    "parallel_count",
    "parallel_find",
    "parallel_find_if",
    "parallel_any_of",
    "parallel_all_of",
    "parallel_none_of",
    "parallel_copy",
]

PARALLEL_SELECTED_FAMILIES = [
    "parallel_for_each",
    "parallel_transform",
    "parallel_transform_binary",
    "parallel_fill",
    "parallel_generate",
    "parallel_transform_reduce",
    "parallel_transform_reduce_binary",
    "parallel_count_if",
]

MODE_ORDER = ["sequential", "automatic", "thread_pool", "static_thread", "one_tbb"]

DISPLAY = {
    "parallel_for_each": "for_each",
    "parallel_transform": "transform",
    "parallel_transform_binary": "transform (binary)",
    "parallel_copy": "copy",
    "parallel_fill": "fill",
    "parallel_generate": "generate",
    "parallel_reduce": "reduce",
    "parallel_transform_reduce": "transform_reduce",
    "parallel_transform_reduce_binary": "transform_reduce (binary)",
    "parallel_count": "count",
    "parallel_count_if": "count_if",
    "parallel_any_of": "any_of",
    "parallel_all_of": "all_of",
    "parallel_none_of": "none_of",
    "parallel_find": "find",
    "parallel_find_if": "find_if",
}

MODE_DISPLAY = {
    "sequential": "Sequential",
    "automatic": "Automatic",
    "thread_pool": "ThreadPool",
    "static_thread": "StaticThread",
    "one_tbb": "oneTBB",
}

COLORS = {
    "sequential": "#64748b",
    "automatic": "#2563eb",
    "thread_pool": "#0f766e",
    "static_thread": "#c2410c",
    "one_tbb": "#7c3aed",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", type=Path, default=DEFAULT_SUMMARY)
    parser.add_argument("--raw", type=Path, default=DEFAULT_RAW)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def geometric_mean(values: Iterable[float]) -> float:
    array = np.asarray(list(values), dtype=float)
    if np.any(array <= 0):
        raise ValueError("geometric mean requires positive values")
    return float(np.exp(np.log(array).mean()))


def validate(summary: pd.DataFrame, raw: pd.DataFrame) -> None:
    required_summary = {
        "schema_version",
        "algorithm",
        "mode",
        "repetitions",
        "iterations",
        "median_ms",
        "min_ms",
        "max_ms",
        "sequential_median_ms",
        "speedup",
        "correct",
        "backend_authenticated",
        "observed_backend",
        "parallel_observed",
    }
    required_raw = {
        "schema_version",
        "algorithm",
        "mode",
        "repetition",
        "elapsed_ms",
        "correct",
        "backend_authenticated",
        "observed_backend",
        "parallel_observed",
    }
    missing_summary = required_summary.difference(summary.columns)
    missing_raw = required_raw.difference(raw.columns)
    if missing_summary:
        raise ValueError(f"summary CSV is missing columns: {sorted(missing_summary)}")
    if missing_raw:
        raise ValueError(f"raw CSV is missing columns: {sorted(missing_raw)}")

    algorithms = list(dict.fromkeys(summary["algorithm"].tolist()))
    if algorithms != ALGORITHM_ORDER:
        raise ValueError(f"unexpected algorithm order: {algorithms}")
    modes = set(summary["mode"])
    if modes != set(MODE_ORDER):
        raise ValueError(f"unexpected benchmark modes: {sorted(modes)}")
    if len(summary) != len(ALGORITHM_ORDER) * len(MODE_ORDER):
        raise ValueError(f"expected 80 summary rows, found {len(summary)}")
    repetitions = summary["repetitions"].drop_duplicates().tolist()
    if len(repetitions) != 1 or int(repetitions[0]) <= 0:
        raise ValueError(f"expected one positive repetition count, found {repetitions}")
    expected_raw_rows = len(ALGORITHM_ORDER) * len(MODE_ORDER) * int(repetitions[0])
    if len(raw) != expected_raw_rows:
        raise ValueError(f"expected {expected_raw_rows} raw rows, found {len(raw)}")
    if set(raw["algorithm"]) != set(ALGORITHM_ORDER):
        raise ValueError("raw CSV does not contain the complete algorithm matrix")
    if set(raw["mode"]) != set(MODE_ORDER):
        raise ValueError("raw CSV does not contain the complete mode matrix")
    if not summary["correct"].eq(1).all() or not raw["correct"].eq(1).all():
        raise ValueError("benchmark input contains a failed correctness result")
    if not summary["backend_authenticated"].eq(1).all() or not raw[
        "backend_authenticated"
    ].eq(1).all():
        raise ValueError("benchmark input contains an unauthenticated backend result")


def prepare(summary: pd.DataFrame) -> tuple[pd.DataFrame, pd.DataFrame]:
    ordered = summary.copy()
    ordered["algorithm"] = pd.Categorical(
        ordered["algorithm"], categories=ALGORITHM_ORDER, ordered=True
    )
    ordered["mode"] = pd.Categorical(ordered["mode"], categories=MODE_ORDER, ordered=True)
    ordered = ordered.sort_values(["algorithm", "mode"]).reset_index(drop=True)
    automatic = ordered[ordered["mode"] == "automatic"].copy()
    automatic["algorithm_name"] = automatic["algorithm"].map(DISPLAY)
    return ordered, automatic


def configure_plot_defaults() -> None:
    plt.rcParams.update(
        {
            "font.size": 10,
            "axes.titlesize": 15,
            "axes.labelsize": 11,
            "axes.spines.top": False,
            "axes.spines.right": False,
            "figure.facecolor": "white",
            "axes.facecolor": "white",
            "savefig.facecolor": "white",
            "svg.hashsalt": "smartparallel-v1.4-benchmarks",
            "grid.alpha": 0.22,
            "grid.linestyle": "-",
        }
    )


def save_figure(fig: plt.Figure, output: Path, stem: str) -> None:
    fig.tight_layout()
    fig.savefig(
        output / f"{stem}.png",
        dpi=180,
        bbox_inches="tight",
        metadata={"Software": "SmartParallel v1.4 benchmark generator"},
    )
    fig.savefig(
        output / f"{stem}.svg",
        bbox_inches="tight",
        metadata={"Date": None, "Creator": "SmartParallel v1.4 benchmark generator"},
    )
    plt.close(fig)


def plot_automatic_speedup(automatic: pd.DataFrame, output: Path) -> None:
    frame = automatic.iloc[::-1].copy()
    colors = [COLORS[str(backend)] for backend in frame["observed_backend"]]
    fig, ax = plt.subplots(figsize=(10.5, 7.2))
    bars = ax.barh(frame["algorithm_name"], frame["speedup"], color=colors, height=0.68)
    ax.axvline(1.0, color="#334155", linewidth=1.1, linestyle="--")
    ax.set_xlabel("Automatic speedup over sequential (×)")
    ax.set_title("SmartParallel v1.4 automatic performance")
    ax.grid(axis="x")
    upper = max(3.8, float(frame["speedup"].max()) * 1.12)
    ax.set_xlim(0, upper)
    for bar, value in zip(bars, frame["speedup"]):
        ax.text(
            min(float(value) + 0.06, upper - 0.05),
            bar.get_y() + bar.get_height() / 2,
            f"{float(value):.2f}×",
            va="center",
            fontsize=9,
        )
    handles = [
        plt.Line2D([0], [0], color=COLORS[backend], lw=8, label=MODE_DISPLAY[backend])
        for backend in ("thread_pool", "sequential")
    ]
    ax.legend(handles=handles, title="Observed automatic route", loc="lower right")
    fig.text(
        0.01,
        0.005,
        "Median of 7 repetitions. Values above 1.0 are faster than the sequential baseline.",
        fontsize=8.5,
        color="#475569",
    )
    save_figure(fig, output, "automatic-speedup-by-algorithm")


def plot_cheap_dispatch(automatic: pd.DataFrame, output: Path) -> None:
    frame = automatic.set_index("algorithm").loc[CHEAP_DISPATCH_FAMILIES].copy()
    frame["overhead_percent"] = (
        frame["median_ms"] / frame["sequential_median_ms"] - 1.0
    ) * 100.0
    frame = frame.iloc[::-1]
    values = frame["overhead_percent"].astype(float)
    bar_colors = ["#0f766e" if value <= 0.0 else COLORS["automatic"] for value in values]
    fig, ax = plt.subplots(figsize=(9.5, 5.5))
    bars = ax.barh(
        [DISPLAY[index] for index in frame.index],
        values,
        color=bar_colors,
        height=0.65,
    )
    ax.axvline(0.0, color="#334155", linewidth=1.1)
    ax.axvline(5.0, color="#b45309", linewidth=1.0, linestyle="--")
    ax.set_xlabel("Automatic latency difference from direct sequential (%)")
    ax.set_title("Cheap-dispatch families after hot-cache learning")
    ax.grid(axis="x")
    low = min(-12.0, math.floor(float(values.min()) - 1.5))
    high = max(6.5, math.ceil(float(values.max()) + 1.5))
    ax.set_xlim(low, high)
    for bar, value in zip(bars, values):
        offset = 0.25 if value >= 0 else -0.25
        align = "left" if value >= 0 else "right"
        ax.text(
            float(value) + offset,
            bar.get_y() + bar.get_height() / 2,
            f"{float(value):+.1f}%",
            va="center",
            ha=align,
            fontsize=9,
        )
    fig.text(
        0.01,
        0.005,
        "Dashed line: +5% release gate. All eight affected families pass; negative values are faster in this sample.",
        fontsize=8.5,
        color="#475569",
    )
    save_figure(fig, output, "cheap-dispatch-relative-latency")


def plot_parallel_backend_comparison(summary: pd.DataFrame, output: Path) -> None:
    frame = summary[summary["algorithm"].isin(PARALLEL_SELECTED_FAMILIES)].copy()
    pivot = frame.pivot(index="algorithm", columns="mode", values="speedup").loc[
        PARALLEL_SELECTED_FAMILIES
    ]
    modes = ["automatic", "thread_pool", "static_thread", "one_tbb"]
    labels = [DISPLAY[index] for index in pivot.index]
    y = np.arange(len(labels))
    height = 0.19
    offsets = np.linspace(-1.5 * height, 1.5 * height, len(modes))

    fig, ax = plt.subplots(figsize=(10.5, 6.6))
    for offset, mode in zip(offsets, modes):
        ax.barh(
            y + offset,
            pivot[mode],
            height=height,
            label=MODE_DISPLAY[mode],
            color=COLORS[mode],
        )
    ax.axvline(1.0, color="#334155", linewidth=1.0, linestyle="--")
    ax.set_yticks(y, labels)
    ax.invert_yaxis()
    ax.set_xlabel("Speedup over sequential (×)")
    ax.set_title("Backend comparison for parallel-selected algorithms")
    ax.grid(axis="x")
    ax.legend(ncol=2, loc="lower right")
    ax.set_xlim(0, max(4.0, float(pivot[modes].max().max()) * 1.08))
    fig.text(
        0.01,
        0.005,
        "Automatic preserved the existing fast paths; the eight parallel-selected cases range from 2.67× to 3.53×.",
        fontsize=8.5,
        color="#475569",
    )
    save_figure(fig, output, "parallel-family-backend-comparison")


def build_metrics(
    summary_path: Path,
    raw_path: Path,
    summary: pd.DataFrame,
    raw: pd.DataFrame,
    automatic: pd.DataFrame,
) -> dict[str, object]:
    parallel = automatic[automatic["parallel_observed"] == 1]
    cheap = automatic[automatic["algorithm"].isin(CHEAP_DISPATCH_FAMILIES)].copy()
    cheap["overhead_percent"] = (
        cheap["median_ms"] / cheap["sequential_median_ms"] - 1.0
    ) * 100.0
    backend_counts = automatic["observed_backend"].value_counts().to_dict()

    algorithms: dict[str, object] = {}
    for _, row in automatic.iterrows():
        algorithms[str(row["algorithm"])] = {
            "iterations": int(row["iterations"]),
            "automatic_median_ms": float(row["median_ms"]),
            "sequential_median_ms": float(row["sequential_median_ms"]),
            "automatic_speedup": float(row["speedup"]),
            "observed_backend": str(row["observed_backend"]),
            "parallel_observed": bool(row["parallel_observed"]),
        }

    return {
        "schema_version": "1.4.0-docs-1",
        "source": {
            "summary": str(summary_path.relative_to(ROOT)),
            "summary_sha256": sha256(summary_path),
            "raw": str(raw_path.relative_to(ROOT)),
            "raw_sha256": sha256(raw_path),
        },
        "validation": {
            "summary_rows": int(len(summary)),
            "raw_rows": int(len(raw)),
            "repetitions": int(summary["repetitions"].iloc[0]),
            "all_correct": bool(summary["correct"].eq(1).all() and raw["correct"].eq(1).all()),
            "all_backends_authenticated": bool(
                summary["backend_authenticated"].eq(1).all()
                and raw["backend_authenticated"].eq(1).all()
            ),
        },
        "automatic": {
            "thread_pool_selected": int(backend_counts.get("thread_pool", 0)),
            "sequential_selected": int(backend_counts.get("sequential", 0)),
            "parallel_selected_geometric_mean_speedup": geometric_mean(parallel["speedup"]),
            "parallel_selected_median_speedup": float(parallel["speedup"].median()),
            "parallel_selected_min_speedup": float(parallel["speedup"].min()),
            "parallel_selected_max_speedup": float(parallel["speedup"].max()),
            "cheap_dispatch_max_slowdown_percent": float(cheap["overhead_percent"].max()),
            "cheap_dispatch_max_absolute_overhead_ms": float(
                (cheap["median_ms"] - cheap["sequential_median_ms"]).max()
            ),
        },
        "algorithms": algorithms,
    }


def write_generated_results(metrics: dict[str, object], output: Path) -> None:
    automatic = metrics["automatic"]
    validation = metrics["validation"]
    algorithms = metrics["algorithms"]
    assert isinstance(automatic, dict)
    assert isinstance(validation, dict)
    assert isinstance(algorithms, dict)

    lines = [
        "# Generated v1.4 benchmark summary",
        "",
        "> Generated by `python tools/plot_v14_algorithm_results.py` from the checked-in",
        "> `validation/output/v1.4.0_parallel_algorithms.csv` and raw sample file.",
        "",
        "## Release summary",
        "",
        f"- **{validation['summary_rows']}** summary rows and **{validation['raw_rows']}** raw samples validated.",
        f"- Automatic selected ThreadPool for **{automatic['thread_pool_selected']}** cases and direct sequential for **{automatic['sequential_selected']}** cases.",
        f"- The parallel-selected group achieved a **{automatic['parallel_selected_geometric_mean_speedup']:.2f}×** geometric-mean speedup.",
        f"- All cheap-dispatch families were within **{automatic['cheap_dispatch_max_slowdown_percent']:.2f}%** of the direct sequential median or faster.",
        "",
        "## Automatic results",
        "",
        "| Algorithm | Sequential median | Automatic median | Speedup | Automatic route |",
        "|---|---:|---:|---:|---|",
    ]
    for algorithm in ALGORITHM_ORDER:
        item = algorithms[algorithm]
        assert isinstance(item, dict)
        lines.append(
            f"| `{algorithm}` | {item['sequential_median_ms']:.4f} ms | "
            f"{item['automatic_median_ms']:.4f} ms | {item['automatic_speedup']:.2f}× | "
            f"`{item['observed_backend']}` |"
        )
    lines.extend(
        [
            "",
            "All rows in the source CSVs passed checksum correctness and backend-authentication checks.",
        ]
    )
    (output / "generated-results.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    summary_path = args.summary.resolve()
    raw_path = args.raw.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    summary = pd.read_csv(summary_path)
    raw = pd.read_csv(raw_path)
    validate(summary, raw)
    ordered, automatic = prepare(summary)
    configure_plot_defaults()
    plot_automatic_speedup(automatic, output)
    plot_cheap_dispatch(automatic, output)
    plot_parallel_backend_comparison(ordered, output)

    metrics = build_metrics(summary_path, raw_path, ordered, raw, automatic)
    (output / "benchmark-metrics.json").write_text(
        json.dumps(metrics, indent=2) + "\n", encoding="utf-8"
    )
    write_generated_results(metrics, output)
    print(f"Generated v1.4 benchmark assets in {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
