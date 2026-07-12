from __future__ import annotations

from pathlib import Path
from typing import Iterable, Sequence

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

EXECUTION_COLUMNS = [
    ("sequential_ms", "Sequential"),
    ("raw_threaded_ms", "Raw threaded"),
    ("static_thread_ms", "StaticThread"),
    ("onetbb_ms", "oneTBB"),
    ("smart_total_ms", "SmartParallel"),
]


def load_results(csv_path: Path) -> pd.DataFrame:
    if not csv_path.exists():
        raise FileNotFoundError(f"Missing results file: {csv_path}")
    return pd.read_csv(csv_path)


def prepare_output(output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)


def save_figure(path: Path) -> None:
    plt.tight_layout()
    plt.savefig(path, dpi=180, bbox_inches="tight")
    plt.close()


def apply_log_x_if_useful(ax: plt.Axes, values: Sequence[float]) -> None:
    positive = [float(v) for v in values if float(v) > 0]
    if len(positive) >= 2 and max(positive) / min(positive) >= 20:
        ax.set_xscale("log")


def apply_log_y_if_useful(ax: plt.Axes, values: Iterable[float]) -> None:
    positive = [float(v) for v in values if float(v) > 0]
    if len(positive) >= 2 and max(positive) / min(positive) >= 100:
        ax.set_yscale("log")


def plot_execution_times(
    df: pd.DataFrame,
    x_column: str,
    x_label: str,
    title: str,
    output_path: Path,
) -> None:
    fig, ax = plt.subplots(figsize=(10, 6))
    x = df[x_column]
    all_values: list[float] = []

    for column, label in EXECUTION_COLUMNS:
        if column in df.columns:
            ax.plot(x, df[column], marker="o", label=label)
            all_values.extend(df[column].astype(float).tolist())

    ax.set_title(title)
    ax.set_xlabel(x_label)
    ax.set_ylabel("Average execution time (ms)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    apply_log_x_if_useful(ax, x.tolist())
    apply_log_y_if_useful(ax, all_values)
    save_figure(output_path)


def plot_smart_breakdown(
    df: pd.DataFrame,
    x_column: str,
    x_label: str,
    title: str,
    output_path: Path,
) -> None:
    fig, ax = plt.subplots(figsize=(10, 6))
    x = np.arange(len(df))
    execution = df["smart_execution_ms"].astype(float)
    overhead = df["smart_overhead_ms"].astype(float)

    ax.bar(x, execution, label="Execution")
    ax.bar(x, overhead, bottom=execution, label="Framework overhead")
    ax.set_xticks(x)
    ax.set_xticklabels([f"{v:g}" for v in df[x_column]], rotation=35, ha="right")
    ax.set_title(title)
    ax.set_xlabel(x_label)
    ax.set_ylabel("Average time (ms)")
    ax.grid(True, axis="y", alpha=0.3)
    ax.legend()
    apply_log_y_if_useful(ax, df["smart_total_ms"].tolist())
    save_figure(output_path)


def plot_gap(
    df: pd.DataFrame,
    x_column: str,
    x_label: str,
    title: str,
    output_path: Path,
) -> None:
    fig, ax = plt.subplots(figsize=(10, 5.5))
    ax.plot(df[x_column], df["smart_gap_percent"], marker="o")
    ax.axhline(0.0, linewidth=1)
    ax.set_title(title)
    ax.set_xlabel(x_label)
    ax.set_ylabel("SmartParallel gap vs best (%)")
    ax.grid(True, alpha=0.3)
    apply_log_x_if_useful(ax, df[x_column].tolist())
    save_figure(output_path)


def plot_decisions(
    df: pd.DataFrame,
    x_column: str,
    x_label: str,
    title: str,
    output_path: Path,
) -> None:
    labels = (
        df["chosen_engine"].astype(str)
        + " / "
        + df["chosen_strategy"].astype(str)
    )
    categories = list(dict.fromkeys(labels.tolist()))
    category_ids = [categories.index(label) for label in labels]

    fig, ax = plt.subplots(figsize=(10, 5.5))
    ax.scatter(df[x_column], category_ids, s=70)
    ax.set_yticks(range(len(categories)))
    ax.set_yticklabels(categories)
    ax.set_title(title)
    ax.set_xlabel(x_label)
    ax.set_ylabel("Selected execution plan")
    ax.grid(True, alpha=0.3)
    apply_log_x_if_useful(ax, df[x_column].tolist())
    save_figure(output_path)


def plot_standard_benchmark(
    csv_path: Path,
    output_dir: Path,
    benchmark_title: str,
    x_column: str = "size",
    x_label: str = "Workload size",
) -> None:
    prepare_output(output_dir)
    df = load_results(csv_path)

    plot_execution_times(
        df,
        x_column,
        x_label,
        f"{benchmark_title}: execution times",
        output_dir / "execution_times.png",
    )
    plot_smart_breakdown(
        df,
        x_column,
        x_label,
        f"{benchmark_title}: SmartParallel time breakdown",
        output_dir / "smart_breakdown.png",
    )
    plot_gap(
        df,
        x_column,
        x_label,
        f"{benchmark_title}: gap to best measured implementation",
        output_dir / "smart_gap.png",
    )
    plot_decisions(
        df,
        x_column,
        x_label,
        f"{benchmark_title}: selected execution plans",
        output_dir / "decision_plan.png",
    )


def plot_tiny_vs_heavy(csv_path: Path, output_dir: Path) -> None:
    prepare_output(output_dir)
    df = load_results(csv_path)

    for function_name in df["function"].drop_duplicates():
        subset = df[df["function"] == function_name].copy()
        slug = str(function_name).lower().replace(" ", "_")
        title_name = str(function_name).title()

        plot_execution_times(
            subset,
            "size",
            "Workload size",
            f"Tiny vs Heavy ({title_name}): execution times",
            output_dir / f"{slug}_execution_times.png",
        )
        plot_smart_breakdown(
            subset,
            "size",
            "Workload size",
            f"Tiny vs Heavy ({title_name}): SmartParallel breakdown",
            output_dir / f"{slug}_smart_breakdown.png",
        )
        plot_gap(
            subset,
            "size",
            "Workload size",
            f"Tiny vs Heavy ({title_name}): gap to best",
            output_dir / f"{slug}_smart_gap.png",
        )
        plot_decisions(
            subset,
            "size",
            "Workload size",
            f"Tiny vs Heavy ({title_name}): selected plans",
            output_dir / f"{slug}_decision_plan.png",
        )


def plot_function_profiler(csv_path: Path, output_dir: Path) -> None:
    prepare_output(output_dir)
    df = load_results(csv_path)

    for iterations in sorted(df["iterations"].unique()):
        subset = df[df["iterations"] == iterations].copy()
        subset = subset.sort_values("parallel_worthiness", ascending=True)

        fig, ax = plt.subplots(figsize=(11, 8))
        ax.barh(subset["function"], subset["parallel_worthiness"])
        ax.set_title(f"Function Profiler: parallel worthiness ({iterations:g} iterations)")
        ax.set_xlabel("Parallel worthiness")
        ax.set_ylabel("Function")
        ax.grid(True, axis="x", alpha=0.3)
        apply_log_x_if_useful(ax, subset["parallel_worthiness"].tolist())
        save_figure(output_dir / f"worthiness_{int(iterations)}.png")

    largest = int(df["iterations"].max())
    subset = df[df["iterations"] == largest].copy()
    subset = subset.sort_values("avg_ms_per_iteration", ascending=True)

    fig, ax = plt.subplots(figsize=(11, 8))
    ax.barh(subset["function"], subset["avg_ms_per_iteration"])
    ax.set_title(f"Function Profiler: average cost per iteration ({largest:g} iterations)")
    ax.set_xlabel("Average time per iteration (ms)")
    ax.set_ylabel("Function")
    ax.grid(True, axis="x", alpha=0.3)
    apply_log_x_if_useful(ax, subset["avg_ms_per_iteration"].tolist())
    save_figure(output_dir / "average_iteration_cost.png")

    stability = (
        df.groupby("expected", as_index=False)["stable"]
        .mean()
        .sort_values("stable", ascending=False)
    )
    fig, ax = plt.subplots(figsize=(9, 5.5))
    ax.bar(stability["expected"], stability["stable"] * 100.0)
    ax.set_title("Function Profiler: stable profiles by workload category")
    ax.set_xlabel("Expected workload category")
    ax.set_ylabel("Stable profiles (%)")
    ax.set_ylim(0, 100)
    ax.tick_params(axis="x", rotation=30)
    ax.grid(True, axis="y", alpha=0.3)
    save_figure(output_dir / "profile_stability.png")


def plot_memory_throughput(csv_path: Path, output_dir: Path) -> None:
    df = load_results(csv_path)
    prepare_output(output_dir)

    fig, ax = plt.subplots(figsize=(10, 6))
    for column, label in EXECUTION_COLUMNS:
        if column not in df.columns:
            continue
        seconds = df[column].astype(float) / 1000.0
        gib = df["bytes_processed"].astype(float) / (1024.0 ** 3)
        throughput = gib / seconds
        ax.plot(df["size"], throughput, marker="o", label=label)

    ax.set_title("Memory Bandwidth: effective throughput")
    ax.set_xlabel("Number of integers")
    ax.set_ylabel("Effective throughput (GiB/s)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    apply_log_x_if_useful(ax, df["size"].tolist())
    save_figure(output_dir / "effective_throughput.png")


def plot_engineering_mesh(csv_path: Path, output_dir: Path) -> None:
    plot_standard_benchmark(
        csv_path,
        output_dir,
        "Engineering Mesh",
        x_column="elements",
        x_label="Mesh elements",
    )

    df = load_results(csv_path)
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.scatter(df["elements"], df["smart_total_ms"], s=df["segments"] * 1.5)
    for _, row in df.iterrows():
        ax.annotate(
            f"{int(row['segments'])} segments",
            (row["elements"], row["smart_total_ms"]),
            xytext=(5, 5),
            textcoords="offset points",
            fontsize=8,
        )
    ax.set_title("Engineering Mesh: SmartParallel scaling")
    ax.set_xlabel("Mesh elements")
    ax.set_ylabel("SmartParallel total time (ms)")
    ax.grid(True, alpha=0.3)
    apply_log_x_if_useful(ax, df["elements"].tolist())
    apply_log_y_if_useful(ax, df["smart_total_ms"].tolist())
    save_figure(output_dir / "mesh_scaling.png")
