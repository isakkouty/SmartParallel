from __future__ import annotations

import argparse
from pathlib import Path
import sys

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from common.plotting import (  # noqa: E402
    plot_engineering_mesh,
    plot_function_profiler,
    plot_memory_throughput,
    plot_standard_benchmark,
    plot_tiny_vs_heavy,
)

VERSION_FOLDER = "beta_1_0"

STANDARD_BENCHMARKS = {
    "small_workloads": ("Small Workloads", "size", "Workload size"),
    "compute_heavy": ("Compute Heavy", "size", "Workload size"),
    "mixed_workload": ("Mixed Workload", "size", "Workload size"),
    "nested_loops": ("Nested Loops", "size", "Workload size"),
    "irregular_workload": ("Irregular Workload", "size", "Workload size"),
    "pair_workloads": ("Pair Workloads", "total_pairs", "Total element pairs"),
    "memory_bandwidth": ("Memory Bandwidth", "size", "Number of integers"),
}


def csv_path(folder: Path) -> Path:
    return folder / "output" / VERSION_FOLDER / "results.csv"


def image_path(folder: Path) -> Path:
    return folder / "images" / VERSION_FOLDER


def generate_all(root: Path) -> list[str]:
    generated: list[str] = []

    for folder_name, (title, x_column, x_label) in STANDARD_BENCHMARKS.items():
        folder = root / folder_name
        source = csv_path(folder)
        destination = image_path(folder)
        plot_standard_benchmark(source, destination, title, x_column, x_label)

        if folder_name == "memory_bandwidth":
            plot_memory_throughput(source, destination)

        generated.append(folder_name)

    tiny_folder = root / "tiny_vs_heavy"
    plot_tiny_vs_heavy(csv_path(tiny_folder), image_path(tiny_folder))
    generated.append("tiny_vs_heavy")

    profiler_folder = root / "function_profiler"
    plot_function_profiler(csv_path(profiler_folder), image_path(profiler_folder))
    generated.append("function_profiler")

    mesh_folder = root / "engineering_mesh"
    plot_engineering_mesh(csv_path(mesh_folder), image_path(mesh_folder))
    generated.append("engineering_mesh")

    return generated


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate all SmartParallel benchmark figures."
    )
    parser.add_argument(
        "--benchmarks-dir",
        type=Path,
        default=SCRIPT_DIR,
        help="Path to the benchmarks directory (default: this script's directory).",
    )
    args = parser.parse_args()

    root = args.benchmarks_dir.resolve()

    try:
        generated = generate_all(root)
    except (FileNotFoundError, KeyError, ValueError) as error:
        print(f"Plot generation failed: {error}", file=sys.stderr)
        return 1

    print("Generated benchmark images for:")
    for benchmark in generated:
        print(f"  - {benchmark}")
    print(f"\nImages are stored under each benchmark's images/{VERSION_FOLDER}/ folder.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
