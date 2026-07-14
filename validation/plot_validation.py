from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd

ROOT = Path(__file__).resolve().parent
OUTPUT = ROOT / "output"
IMAGES = ROOT / "images"
IMAGES.mkdir(parents=True, exist_ok=True)


def plot_suite(summary_path: Path, candidates_path: Path, prefix: str, title: str) -> None:
    if not summary_path.exists() or not candidates_path.exists():
        print(f"Skipping {title}: CSV files are not available yet.")
        return

    summary = pd.read_csv(summary_path)
    candidates = pd.read_csv(candidates_path)

    error_column = (
        "winner_prediction_error_percent"
        if "winner_prediction_error_percent" in summary.columns
        else "prediction_error_percent"
    )
    regret_column = (
        "selected_plan_regret_percent"
        if "selected_plan_regret_percent" in summary.columns
        else "regret_percent"
    )

    labels = summary["case"]
    x = range(len(summary))

    fig, ax = plt.subplots(figsize=(13, 6))
    ax.bar(x, summary[error_column])
    ax.set_xticks(list(x))
    ax.set_xticklabels(labels, rotation=45, ha="right")
    ax.set_ylabel("Absolute prediction error (%)")
    ax.set_title(f"{title}: prediction error by workload")
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(IMAGES / f"{prefix}_prediction_error.png", dpi=180)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(13, 6))
    ax.bar(x, summary[regret_column])
    ax.set_xticks(list(x))
    ax.set_xticklabels(labels, rotation=45, ha="right")
    ax.set_ylabel("Selected-plan regret (%)")
    ax.set_title(f"{title}: cost of choosing the predicted plan")
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(IMAGES / f"{prefix}_decision_regret.png", dpi=180)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(8, 7))
    ax.scatter(candidates["actual_ms"], candidates["predicted_runtime_ms"])
    maximum = max(
        candidates["actual_ms"].max(),
        candidates["predicted_runtime_ms"].max(),
    )
    ax.plot([0, maximum], [0, maximum], linestyle="--")
    ax.set_xlabel("Measured runtime (ms)")
    ax.set_ylabel("Predicted runtime (ms)")
    ax.set_title(f"{title}: predicted versus measured runtime")
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(IMAGES / f"{prefix}_predicted_vs_measured.png", dpi=180)
    plt.close(fig)

    if "suite" in summary.columns:
        grouped = summary.groupby("suite", as_index=False).agg(
            cases=("case", "count"),
            mean_regret=(regret_column, "mean"),
            median_error=(error_column, "median"),
            near_accuracy=("within_3_percent", "mean"),
        )
        grouped["near_accuracy"] *= 100.0

        fig, ax = plt.subplots(figsize=(10, 6))
        ax.bar(grouped["suite"], grouped["mean_regret"])
        ax.set_ylabel("Mean regret (%)")
        ax.set_title(f"{title}: mean regret by workload family")
        ax.tick_params(axis="x", rotation=35)
        ax.grid(axis="y", alpha=0.3)
        fig.tight_layout()
        fig.savefig(IMAGES / f"{prefix}_regret_by_suite.png", dpi=180)
        plt.close(fig)

        grouped.to_csv(OUTPUT / f"{prefix}_suite_metrics.csv", index=False)


plot_suite(
    OUTPUT / "prediction_summary.csv",
    OUTPUT / "prediction_candidates.csv",
    "calibration",
    "Calibration suite",
)

plot_suite(
    OUTPUT / "holdout_summary.csv",
    OUTPUT / "holdout_candidates.csv",
    "holdout",
    "Holdout suite",
)

print(f"Generated validation images in {IMAGES}")

ranking_summary_path = OUTPUT / "ranking_evolution_summary.csv"
ranking_candidates_path = OUTPUT / "ranking_evolution_candidates.csv"

if ranking_summary_path.exists() and ranking_candidates_path.exists():
    ranking_summary = pd.read_csv(ranking_summary_path)
    ranking_candidates = pd.read_csv(ranking_candidates_path)

    fig, ax = plt.subplots(figsize=(10, 6))
    for case_name, group in ranking_summary.groupby("case"):
        ax.plot(group["round"], group["regret_percent"], marker="o", label=case_name)
    ax.set_xlabel("Learning round")
    ax.set_ylabel("Selected-plan regret (%)")
    ax.set_title("Phase 4: regret evolution with historical ranking")
    ax.grid(alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(IMAGES / "ranking_regret_evolution.png", dpi=180)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(10, 6))
    for case_name, group in ranking_summary.groupby("case"):
        ax.plot(group["round"], group["history_weight"], marker="o", label=case_name)
    ax.set_xlabel("Learning round")
    ax.set_ylabel("Historical ranking weight")
    ax.set_title("Phase 4: historical ranking weight growth")
    ax.grid(alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(IMAGES / "ranking_history_weight.png", dpi=180)
    plt.close(fig)

    final_round = ranking_summary["round"].max()
    comparison = ranking_summary[
        ranking_summary["round"].isin([0, final_round])
    ].pivot(index="case", columns="round", values="regret_percent")
    comparison.columns = ["cold_start", "final_round"]
    comparison.plot(kind="bar", figsize=(10, 6))
    plt.ylabel("Selected-plan regret (%)")
    plt.title("Phase 4: cold-start versus learned regret")
    plt.xticks(rotation=35, ha="right")
    plt.grid(axis="y", alpha=0.3)
    plt.tight_layout()
    plt.savefig(IMAGES / "ranking_cold_vs_learned.png", dpi=180)
    plt.close()
else:
    print("Skipping ranking evolution plots: CSV files are not available yet.")
