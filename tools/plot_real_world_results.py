#!/usr/bin/env python3
"""Generate the SmartParallel v1.1 real-world benchmark figures.

The script reads the recorded final-run CSV files, validates their schemas and
correctness fields, writes a compact machine-readable metrics file, and creates
publication-ready PNG/SVG figures for the release documentation.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Iterable

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

INTEGRATION_ORDER = ["opencv", "lz4", "bvh", "particles"]
DISPLAY_NAMES = {
    "opencv": "OpenCV",
    "lz4": "LZ4",
    "bvh": "BVH",
    "particles": "Particles",
}
REQUIRED_AUTO_COLUMNS = {
    "integration",
    "preset",
    "best_mode",
    "best_median_ms",
    "sequential_median_ms",
    "automatic_backend",
    "automatic_frontier",
    "automatic_median_ms",
    "automatic_p95_ms",
    "automatic_speedup",
    "automatic_regret_ms",
    "automatic_regret_percent",
    "scheduler_assessment",
}
REQUIRED_SUMMARY_COLUMNS = {
    "integration",
    "preset",
    "mode",
    "actual_backend",
    "median_ms",
    "correct",
    "valid_for_ranking",
    "max_concurrency",
}
REQUIRED_TRACE_COLUMNS = {
    "integration",
    "preset",
    "mode",
    "backend",
    "backend_confirmed",
    "max_root_leased_workers",
    "runtime_concurrency",
    "exceptional",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=Path("validation/output/real_world"),
        help="Directory containing the final v1.1 real-world CSV files.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("docs/v1.1/assets/benchmarks"),
        help="Directory for generated figures and metric summaries.",
    )
    return parser.parse_args()


def require_columns(frame: pd.DataFrame, required: Iterable[str], source: Path) -> None:
    missing = sorted(set(required) - set(frame.columns))
    if missing:
        raise ValueError(f"{source} is missing required columns: {', '.join(missing)}")


def read_csv(path: Path, required: Iterable[str]) -> pd.DataFrame:
    if not path.is_file():
        raise FileNotFoundError(f"Required benchmark file not found: {path}")
    frame = pd.read_csv(path)
    if frame.empty:
        raise ValueError(f"Benchmark file is empty: {path}")
    require_columns(frame, required, path)
    return frame


def truthy(series: pd.Series) -> pd.Series:
    if pd.api.types.is_bool_dtype(series):
        return series.fillna(False)
    return series.astype(str).str.strip().str.lower().isin({"1", "true", "yes"})


def validate_results(input_dir: Path) -> tuple[pd.DataFrame, dict[str, pd.DataFrame]]:
    auto_path = input_dir / "v1.1.0_real_world_auto_analysis.csv"
    auto = read_csv(auto_path, REQUIRED_AUTO_COLUMNS)

    numeric = [
        "best_median_ms",
        "sequential_median_ms",
        "automatic_median_ms",
        "automatic_p95_ms",
        "automatic_speedup",
        "automatic_regret_ms",
        "automatic_regret_percent",
    ]
    for column in numeric:
        auto[column] = pd.to_numeric(auto[column], errors="raise")
    if (~np.isfinite(auto[numeric].to_numpy(dtype=float))).any():
        raise ValueError(f"{auto_path} contains non-finite numeric values")
    if (auto["automatic_median_ms"] <= 0).any() or (auto["sequential_median_ms"] <= 0).any():
        raise ValueError(f"{auto_path} contains non-positive median runtimes")

    summaries: dict[str, pd.DataFrame] = {}
    for integration in INTEGRATION_ORDER:
        summary_path = input_dir / f"v1.1.0_real_world_{integration}_summary.csv"
        trace_path = input_dir / f"v1.1.0_real_world_{integration}_trace.csv"
        summary = read_csv(summary_path, REQUIRED_SUMMARY_COLUMNS)
        trace = read_csv(trace_path, REQUIRED_TRACE_COLUMNS)

        if not truthy(summary["correct"]).all():
            raise ValueError(f"Correctness failure recorded in {summary_path}")
        if not truthy(summary["valid_for_ranking"]).all():
            raise ValueError(f"Invalid ranking row recorded in {summary_path}")
        if not truthy(trace["backend_confirmed"]).all():
            raise ValueError(f"Backend-authentication failure recorded in {trace_path}")
        if truthy(trace["exceptional"]).any():
            raise ValueError(f"Exceptional trace row recorded in {trace_path}")

        max_concurrency = pd.to_numeric(summary["max_concurrency"], errors="raise").max()
        max_trace_concurrency = pd.to_numeric(trace["runtime_concurrency"], errors="raise").max()
        max_leases = pd.to_numeric(trace["max_root_leased_workers"], errors="raise").max()
        if max(max_concurrency, max_trace_concurrency, max_leases) > 4:
            raise ValueError(
                f"Recorded concurrency budget exceeded four in {integration}: "
                f"summary={max_concurrency}, trace={max_trace_concurrency}, leases={max_leases}"
            )
        summaries[integration] = summary

    if set(auto["integration"]) != set(INTEGRATION_ORDER):
        raise ValueError("Automatic analysis does not contain exactly the four release integrations")
    return auto, summaries


def label_presets(frame: pd.DataFrame) -> pd.Series:
    return frame["integration"].map(DISPLAY_NAMES) + " — " + frame["preset"].str.replace("_", " ")


def save_figure(fig: plt.Figure, output_dir: Path, stem: str) -> None:
    fig.tight_layout()
    fig.savefig(output_dir / f"{stem}.png", dpi=180, bbox_inches="tight")
    fig.savefig(output_dir / f"{stem}.svg", bbox_inches="tight")
    plt.close(fig)


def plot_speedup_by_preset(auto: pd.DataFrame, output_dir: Path) -> None:
    frame = auto.sort_values("automatic_speedup", ascending=True).copy()
    labels = label_presets(frame)
    fig, ax = plt.subplots(figsize=(10, 9))
    ax.barh(labels, frame["automatic_speedup"])
    ax.axvline(1.0, linewidth=1.0, linestyle="--")
    ax.set_xlim(left=0)
    ax.set_xlabel("Automatic speedup over sequential (median runtime ratio)")
    ax.set_title("SmartParallel v1.1 automatic speedup by workload preset")
    ax.grid(axis="x", alpha=0.25)
    save_figure(fig, output_dir, "automatic-speedup-by-preset")


def plot_integration_medians(auto: pd.DataFrame, output_dir: Path) -> None:
    medians = auto.groupby("integration", sort=False)["automatic_speedup"].median().reindex(INTEGRATION_ORDER)
    fig, ax = plt.subplots(figsize=(8, 5))
    bars = ax.bar([DISPLAY_NAMES[name] for name in medians.index], medians.values)
    ax.axhline(1.0, linewidth=1.0, linestyle="--")
    ax.set_ylim(bottom=0)
    ax.set_ylabel("Median automatic speedup over sequential")
    ax.set_title("Median automatic speedup by integration")
    ax.grid(axis="y", alpha=0.25)
    for bar, value in zip(bars, medians.values):
        ax.text(bar.get_x() + bar.get_width() / 2, value, f"{value:.2f}×", ha="center", va="bottom")
    save_figure(fig, output_dir, "median-speedup-by-integration")


def plot_regret(auto: pd.DataFrame, output_dir: Path) -> None:
    frame = auto.sort_values("automatic_regret_percent", ascending=True).copy()
    labels = label_presets(frame)
    fig, ax = plt.subplots(figsize=(10, 9))
    ax.barh(labels, frame["automatic_regret_percent"])
    ax.axvline(15.0, linewidth=1.0, linestyle="--", label="15% close-to-best threshold")
    ax.set_xlim(left=0)
    ax.set_xlabel("Automatic regret versus fastest valid tested mode (%)")
    ax.set_title("SmartParallel v1.1 automatic regret by workload preset")
    ax.grid(axis="x", alpha=0.25)
    ax.legend(loc="lower right")
    save_figure(fig, output_dir, "automatic-regret-by-preset")


def plot_representative_runtime(auto: pd.DataFrame, output_dir: Path) -> None:
    representatives = [
        ("opencv", "mixed_sizes"),
        ("lz4", "mixed_sizes"),
        ("bvh", "mixed_distribution"),
        ("particles", "sparse"),
        ("particles", "gradual_count_increase"),
    ]
    rows = []
    for integration, preset in representatives:
        match = auto[(auto["integration"] == integration) & (auto["preset"] == preset)]
        if len(match) != 1:
            raise ValueError(f"Representative case missing or duplicated: {integration}/{preset}")
        rows.append(match.iloc[0])
    frame = pd.DataFrame(rows)
    labels = label_presets(frame)
    x = np.arange(len(frame))
    width = 0.38
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.bar(x - width / 2, frame["sequential_median_ms"], width, label="Sequential")
    ax.bar(x + width / 2, frame["automatic_median_ms"], width, label="Automatic SmartParallel")
    ax.set_xticks(x, labels, rotation=25, ha="right")
    ax.set_ylim(bottom=0)
    ax.set_ylabel("Median runtime (ms)")
    ax.set_title("Sequential versus automatic runtime on representative workloads")
    ax.grid(axis="y", alpha=0.25)
    ax.legend()
    save_figure(fig, output_dir, "representative-runtime-comparison")


def plot_backend_selections(auto: pd.DataFrame, output_dir: Path) -> None:
    counts = (
        auto.groupby(["integration", "automatic_backend"]).size().unstack(fill_value=0).reindex(INTEGRATION_ORDER)
    )
    backend_order = [name for name in ["sequential", "thread_pool", "one_tbb", "static_thread"] if name in counts.columns]
    counts = counts.reindex(columns=backend_order, fill_value=0)
    fig, ax = plt.subplots(figsize=(8, 5))
    counts.plot(kind="bar", stacked=True, ax=ax)
    ax.set_xticklabels([DISPLAY_NAMES[name] for name in counts.index], rotation=0)
    ax.set_ylabel("Number of presets")
    ax.set_xlabel("")
    ax.set_title("Automatic backend selected after warm-up")
    ax.grid(axis="y", alpha=0.25)
    ax.legend(title="Backend")
    save_figure(fig, output_dir, "automatic-backend-selection")


def geometric_mean(values: pd.Series) -> float:
    values = pd.to_numeric(values, errors="raise").to_numpy(dtype=float)
    if (values <= 0).any():
        raise ValueError("Geometric mean requires positive values")
    return float(math.exp(np.log(values).mean()))


def write_metrics(auto: pd.DataFrame, summaries: dict[str, pd.DataFrame], output_dir: Path) -> None:
    meaningful = auto[auto["automatic_median_ms"] >= 1.0]
    metrics: dict[str, object] = {
        "source": "validation/output/real_world/v1.1.0_real_world_auto_analysis.csv",
        "preset_count": int(len(auto)),
        "meaningful_preset_count": int(len(meaningful)),
        "all_presets": {
            "geometric_mean_speedup": geometric_mean(auto["automatic_speedup"]),
            "median_regret_percent": float(auto["automatic_regret_percent"].median()),
            "within_15_percent": int((auto["automatic_regret_percent"] <= 15.0).sum()),
            "within_20_percent": int((auto["automatic_regret_percent"] <= 20.0).sum()),
            "faster_than_sequential": int((auto["automatic_speedup"] > 1.0).sum()),
        },
        "meaningful_presets": {
            "definition": "automatic median runtime >= 1 ms",
            "geometric_mean_speedup": geometric_mean(meaningful["automatic_speedup"]),
            "median_regret_percent": float(meaningful["automatic_regret_percent"].median()),
            "mean_regret_percent": float(meaningful["automatic_regret_percent"].mean()),
            "within_15_percent": int((meaningful["automatic_regret_percent"] <= 15.0).sum()),
            "within_20_percent": int((meaningful["automatic_regret_percent"] <= 20.0).sum()),
            "faster_than_sequential": int((meaningful["automatic_speedup"] > 1.0).sum()),
        },
        "integrations": {},
        "validation": {
            "summary_rows": int(sum(len(frame) for frame in summaries.values())),
            "all_correct": True,
            "all_backends_confirmed": True,
            "maximum_concurrency": 4,
            "maximum_root_leases": 4,
        },
    }
    for integration in INTEGRATION_ORDER:
        frame = auto[auto["integration"] == integration]
        metrics["integrations"][integration] = {
            "presets": int(len(frame)),
            "geometric_mean_speedup": geometric_mean(frame["automatic_speedup"]),
            "median_speedup": float(frame["automatic_speedup"].median()),
            "median_regret_percent": float(frame["automatic_regret_percent"].median()),
            "within_15_percent": int((frame["automatic_regret_percent"] <= 15.0).sum()),
            "within_20_percent": int((frame["automatic_regret_percent"] <= 20.0).sum()),
            "faster_than_sequential": int((frame["automatic_speedup"] > 1.0).sum()),
        }

    (output_dir / "benchmark-metrics.json").write_text(
        json.dumps(metrics, indent=2) + "\n", encoding="utf-8"
    )

    lines = [
        "# Generated benchmark summary",
        "",
        "> Generated by `python tools/plot_real_world_results.py` from",
        "> `validation/output/real_world/v1.1.0_real_world_auto_analysis.csv`.",
        "",
        "| Integration | Presets | Geometric-mean speedup | Median speedup | Median regret | Within 15% of best |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for integration in INTEGRATION_ORDER:
        item = metrics["integrations"][integration]
        lines.append(
            f"| {DISPLAY_NAMES[integration]} | {item['presets']} | "
            f"{item['geometric_mean_speedup']:.2f}× | {item['median_speedup']:.2f}× | "
            f"{item['median_regret_percent']:.2f}% | {item['within_15_percent']}/{item['presets']} |"
        )
    lines.extend(
        [
            "",
            "## Per-preset automatic results",
            "",
            "| Integration | Preset | Sequential median | Automatic median | Speedup | Regret | Backend | Frontier |",
            "|---|---|---:|---:|---:|---:|---|---|",
        ]
    )
    for row in auto.sort_values(["integration", "preset"]).itertuples(index=False):
        lines.append(
            f"| {DISPLAY_NAMES[row.integration]} | `{row.preset}` | {row.sequential_median_ms:.4f} ms | "
            f"{row.automatic_median_ms:.4f} ms | {row.automatic_speedup:.2f}× | "
            f"{row.automatic_regret_percent:.2f}% | `{row.automatic_backend}` | `{row.automatic_frontier}` |"
        )
    (output_dir / "generated-results.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    input_dir = args.input_dir.resolve()
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    auto, summaries = validate_results(input_dir)
    plt.rcParams.update({
        "font.size": 10,
        "axes.titlesize": 14,
        "axes.labelsize": 11,
        "legend.fontsize": 9,
    })

    plot_speedup_by_preset(auto, output_dir)
    plot_integration_medians(auto, output_dir)
    plot_regret(auto, output_dir)
    plot_representative_runtime(auto, output_dir)
    plot_backend_selections(auto, output_dir)
    write_metrics(auto, summaries, output_dir)

    print(f"Generated SmartParallel benchmark figures in {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
