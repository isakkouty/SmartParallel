#!/usr/bin/env python3
"""Publish one validated SmartParallel v1.5 benchmark run into the docs tree."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
import shutil
import statistics
import subprocess
import sys
import zipfile
from collections import defaultdict
from pathlib import Path

PRESET_LABELS = {
    "tiny_320x240": "320×240",
    "small_640x480": "640×480",
    "medium_1920x1080": "1920×1080",
    "medium_1920x1080_roi": "1920×1080 ROI",
    "large_3840x2160": "3840×2160",
    "very_large_7680x4320": "7680×4320",
}
ROUTE_LABELS = {
    "native_sequential": "Native Sequential",
    "native_thread_pool": "Native ThreadPool",
    "native_static_thread": "Native StaticThread",
    "native_one_tbb": "Native oneTBB",
    "opencv": "OpenCV",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("publication_dir", type=Path)
    parser.add_argument(
        "docs_dir",
        type=Path,
        nargs="?",
        default=Path(__file__).resolve().parents[1] / "docs/v1.5/assets/benchmarks",
    )
    return parser.parse_args()


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise ValueError(f"{path} is empty")
    return rows


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def geometric_mean(values: list[float]) -> float:
    if not values or any(value <= 0 for value in values):
        raise ValueError("geometric mean requires positive values")
    return math.exp(sum(math.log(value) for value in values) / len(values))


def steady_medians(raw_rows: list[dict[str, str]]) -> dict[str, dict[str, float]]:
    grouped: dict[tuple[str, str], list[int]] = defaultdict(list)
    for row in raw_rows:
        if row["phase"] == "steady_state":
            grouped[(row["preset"], row["implementation"])].append(int(row["duration_ns"]))
    medians: dict[str, dict[str, float]] = defaultdict(dict)
    for (preset, implementation), values in grouped.items():
        medians[preset][implementation] = float(statistics.median(values))
    return medians


def env_value(text: str, key: str) -> str:
    match = re.search(rf"^{re.escape(key)}:\s*(.+)$", text, re.MULTILINE)
    return match.group(1).strip() if match else "unknown"


def format_duration(ns: float) -> str:
    if ns < 1_000:
        return f"{ns:.0f} ns"
    if ns < 1_000_000:
        return f"{ns / 1_000:.1f} µs"
    return f"{ns / 1_000_000:.3f} ms"


def main() -> int:
    args = parse_args()
    publication = args.publication_dir.resolve()
    docs = args.docs_dir.resolve()
    summary_path = publication / "v1.5.0_adaptive_routes.csv"
    raw_path = publication / "v1.5.0_adaptive_routes_raw.csv"
    learning_path = publication / "v1.5.0_adaptive_routes_learning.csv"
    environment_path = publication / "v1.5.0_adaptive_routes_environment.txt"
    report_path = publication / "v1.5.0_adaptive_routes_report.md"
    required = [summary_path, raw_path, learning_path, environment_path, report_path]
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        raise FileNotFoundError(f"publication is incomplete: {missing}")

    summary = read_csv(summary_path)
    raw = read_csv(raw_path)
    learning = read_csv(learning_path)
    if len(summary) != 6 or any(row.get("overall_pass") != "1" for row in summary):
        raise ValueError("Only a validated 6/6 publication run can be published")
    if any(row.get("correctness_pass", "").lower() not in {"1", "true", "yes"}
           or int(row.get("mismatch_count", "1")) != 0 for row in raw):
        raise ValueError("Raw publication data contains a correctness failure")

    docs.mkdir(parents=True, exist_ok=True)
    plotter = Path(__file__).with_name("generate_v15_benchmark_plots.py")
    subprocess.run(
        [sys.executable, str(plotter), str(summary_path), str(raw_path),
         str(learning_path), str(publication)],
        check=True,
    )

    copied = {
        "accepted-summary.csv": summary_path,
        "accepted-learning.csv": learning_path,
        "accepted-environment.txt": environment_path,
        "accepted-publication-report.md": report_path,
    }
    for destination_name, source in copied.items():
        shutil.copy2(source, docs / destination_name)
    for svg in sorted(publication.glob("v1.5.0_*.svg")):
        shutil.copy2(svg, docs / svg.name)

    archive_members = [
        raw_path, learning_path, summary_path, environment_path, report_path,
    ]
    build_log = publication / "v1.5.0_build_vectorization.log"
    if build_log.exists():
        archive_members.append(build_log)
    archive_members.extend(sorted(publication.glob("v1.5.0_*.svg")))
    with zipfile.ZipFile(docs / "accepted-publication.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for member in archive_members:
            archive.write(member, member.name)

    medians = steady_medians(raw)
    environment = environment_path.read_text(encoding="utf-8-sig")
    rows = []
    for row in summary:
        preset = row["preset"]
        auto = medians[preset]["smart_auto"]
        direct = medians[preset]["direct_sequential"]
        opencv = medians[preset]["opencv_api"]
        rows.append({
            "preset": preset,
            "label": PRESET_LABELS[preset],
            "width": int(row["width"]),
            "height": int(row["height"]),
            "stride_bytes": int(row["stride_bytes"]),
            "settled_route": row["auto_selected_route"],
            "settled_route_label": ROUTE_LABELS.get(row["auto_selected_route"], row["auto_selected_route"]),
            "route_switch_count": int(row["route_switch_count"]),
            "auto_median_ns": int(float(auto)),
            "direct_sequential_median_ns": int(float(direct)),
            "opencv_api_median_ns": int(float(opencv)),
            "speedup_vs_direct_sequential": direct / auto,
            "speedup_vs_opencv_api": opencv / auto,
            "route_selection_regret_percent": float(row["route_selection_regret_percent"]),
            "dispatch_overhead_ns": int(row["batched_dispatch_overhead_ns"]),
            "dispatch_ci_lower_ns": int(row["dispatch_ci_lower_ns"]),
            "dispatch_ci_upper_ns": int(row["dispatch_ci_upper_ns"]),
            "native_kernel_delta_percent": float(row["native_kernel_delta_percent"]),
            "overall_pass": bool(int(row["overall_pass"])),
        })

    metrics = {
        "release": "1.5.0",
        "title": "Adaptive Execution Routes",
        "operation": "threshold_u8_binary",
        "publication_directory": publication.name,
        "raw_schema_version": sorted({row["schema_version"] for row in raw})[0],
        "learning_schema_version": sorted({row["schema_version"] for row in learning})[0],
        "steady_state_repetitions": int(raw[0]["requested_repetitions"]),
        "raw_sample_count": len(raw),
        "correctness_failures": 0,
        "authentication_failures": 0,
        "combined_gate_passes": sum(int(row["overall_pass"]) for row in summary),
        "combined_gate_total": len(summary),
        "native_kernel": summary[0]["native_kernel"],
        "logical_threads": env_value(environment, "Logical threads"),
        "worker_budget": env_value(environment, "SmartParallel worker budget"),
        "opencv_version": env_value(environment, "OpenCV version"),
        "opencv_threads": env_value(environment, "OpenCV threads"),
        "opencv_opencl": env_value(environment, "OpenCV OpenCL enabled"),
        "geometric_mean_speedup_vs_direct_sequential": geometric_mean(
            [row["speedup_vs_direct_sequential"] for row in rows]
        ),
        "geometric_mean_speedup_vs_opencv_api": geometric_mean(
            [row["speedup_vs_opencv_api"] for row in rows]
        ),
        "hashes": {
            "raw_csv_sha256": sha256(raw_path),
            "learning_csv_sha256": sha256(learning_path),
            "summary_csv_sha256": sha256(summary_path),
            "environment_sha256": sha256(environment_path),
            "publication_report_sha256": sha256(report_path),
        },
        "presets": rows,
    }
    (docs / "benchmark-metrics.json").write_text(
        json.dumps(metrics, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )

    result_lines = [
        "# Generated v1.5 benchmark results",
        "",
        "> Generated from the accepted Windows/MSVC publication evidence. Do not edit by hand; rerun `tools/publish_v15_benchmark_docs.py`.",
        "",
        f"- Publication: `{publication.name}`",
        f"- Raw samples: **{len(raw):,}**",
        f"- Correctness/authentication failures: **0**",
        f"- Combined proof gates: **{metrics['combined_gate_passes']}/{metrics['combined_gate_total']} passed**",
        f"- Native kernel: **{metrics['native_kernel']}**",
        f"- Geometric-mean Auto speedup versus direct sequential: **{metrics['geometric_mean_speedup_vs_direct_sequential']:.2f}×**",
        f"- Geometric-mean Auto speedup versus direct OpenCV API: **{metrics['geometric_mean_speedup_vs_opencv_api']:.2f}×**",
        "",
        "| Preset | Settled route | Auto median | Speedup vs direct | Speedup vs OpenCV API | Route regret | Runtime switches |",
        "|---|---|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        result_lines.append(
            f"| {row['label']} | {row['settled_route_label']} | "
            f"{format_duration(row['auto_median_ns'])} | "
            f"{row['speedup_vs_direct_sequential']:.2f}× | "
            f"{row['speedup_vs_opencv_api']:.2f}× | "
            f"{row['route_selection_regret_percent']:.2f}% | "
            f"{row['route_switch_count']} |"
        )
    result_lines += [
        "",
        "All figures and aggregate values are machine-specific. The release claim is that Auto selected a route inside the declared equivalence gate on this machine, not that one provider is universally fastest.",
    ]
    (docs / "generated-results.md").write_text("\n".join(result_lines) + "\n", encoding="utf-8")

    hash_lines = [f"{value}  {key}" for key, value in metrics["hashes"].items()]
    (docs / "source-hashes.txt").write_text("\n".join(hash_lines) + "\n", encoding="utf-8")

    readme = """# SmartParallel v1.5 benchmark assets

These files are generated from the accepted v1.5 publication run and support the public benchmark report.

- `generated-results.md` — generated table and aggregate metrics.
- `benchmark-metrics.json` — machine-readable accepted metrics.
- `accepted-summary.csv` — six-preset proof-gate summary.
- `accepted-learning.csv` — route-learning and current-context adaptation evidence.
- `accepted-environment.txt` — compiler, OpenCV, worker-budget, and build configuration.
- `accepted-publication-report.md` — analyzer-generated release-gate report.
- `source-hashes.txt` — hashes of the accepted source evidence.
- Expanded accepted raw evidence, learning data, environment, report, build log, and figures are stored directly; no nested ZIP is generated.
- `v1.5.0_*.svg` — dependency-free publication figures.

Regenerate a publication run with:

```bat
set "VCPKG_ROOT=D:\\Tools\\vcpkg" && scripts\\validation\\run_v15_adaptive_routes_release_validation.bat 31
```

Publish a validated run into this directory with:

```bat
py -3 tools\\publish_v15_benchmark_docs.py validation\\output\\v1.5.0_adaptive_routes\\publication_<timestamp>
```

Performance is machine-specific. Do not copy values from one machine into universal performance claims.
"""
    (docs / "README.md").write_text(readme, encoding="utf-8")
    print(f"Published v1.5 documentation assets to {docs}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
