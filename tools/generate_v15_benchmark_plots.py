#!/usr/bin/env python3
"""Generate dependency-free SVG plots for SmartParallel v1.5 publication data."""

from __future__ import annotations

import argparse
import csv
import html
import math
import statistics
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
    parser.add_argument("summary_csv", type=Path)
    parser.add_argument("raw_csv", type=Path)
    parser.add_argument("learning_csv", type=Path)
    parser.add_argument("output_dir", type=Path)
    return parser.parse_args()


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise ValueError(f"{path} is empty")
    return rows


def median(values: list[int]) -> float:
    if not values:
        raise ValueError("missing samples")
    return float(statistics.median(values))


def svg_header(width: int, height: int, title: str, subtitle: str) -> list[str]:
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="40" y="38" font-family="Segoe UI, Arial, sans-serif" font-size="24" font-weight="700" fill="#17212b">{html.escape(title)}</text>',
        f'<text x="40" y="62" font-family="Segoe UI, Arial, sans-serif" font-size="13" fill="#536273">{html.escape(subtitle)}</text>',
    ]


def write_svg(path: Path, content: list[str]) -> None:
    content.append("</svg>")
    path.write_text("\n".join(content) + "\n", encoding="utf-8")


def steady_medians(raw_rows: list[dict[str, str]]) -> dict[str, dict[str, float]]:
    grouped: dict[tuple[str, str], list[int]] = defaultdict(list)
    for row in raw_rows:
        if row["phase"] == "steady_state":
            grouped[(row["preset"], row["implementation"])].append(int(row["duration_ns"]))
    result: dict[str, dict[str, float]] = defaultdict(dict)
    for (preset, implementation), values in grouped.items():
        result[preset][implementation] = median(values)
    return result


def plot_speedups(summary: list[dict[str, str]], medians: dict[str, dict[str, float]], out: Path) -> None:
    width, height = 1120, 510
    left, top, row_h, plot_w = 210, 95, 62, 790
    data = []
    max_speedup = 1.0
    for row in summary:
        preset = row["preset"]
        auto = medians[preset]["smart_auto"]
        direct = medians[preset]["direct_sequential"]
        opencv = medians[preset]["opencv_api"]
        direct_speedup = direct / auto
        opencv_speedup = opencv / auto
        max_speedup = max(max_speedup, direct_speedup, opencv_speedup)
        data.append((preset, direct_speedup, opencv_speedup))
    max_speedup = math.ceil(max_speedup * 10.0) / 10.0
    content = svg_header(
        width, height,
        "SmartParallel v1.5 automatic speedup",
        "Values above 1.0× mean SmartParallel Auto completed the threshold operation faster.",
    )
    x0 = left
    for tick in range(0, int(max_speedup * 10) + 1, 5):
        value = tick / 10.0
        x = x0 + plot_w * value / max_speedup
        content += [
            f'<line x1="{x:.1f}" y1="{top - 10}" x2="{x:.1f}" y2="{top + row_h * len(data) - 12}" stroke="#e5e9ee" stroke-width="1"/>',
            f'<text x="{x:.1f}" y="{height - 28}" text-anchor="middle" font-family="Segoe UI, Arial, sans-serif" font-size="12" fill="#657384">{value:.1f}×</text>',
        ]
    for i, (preset, direct_speedup, opencv_speedup) in enumerate(data):
        y = top + i * row_h
        content.append(f'<text x="{left - 16}" y="{y + 25}" text-anchor="end" font-family="Segoe UI, Arial, sans-serif" font-size="14" fill="#263442">{PRESET_LABELS[preset]}</text>')
        for offset, value, fill, label in [
            (4, direct_speedup, "#356f9f", "vs direct sequential"),
            (29, opencv_speedup, "#6a9a55", "vs OpenCV API"),
        ]:
            bar = plot_w * value / max_speedup
            content += [
                f'<rect x="{x0}" y="{y + offset}" width="{bar:.1f}" height="18" rx="3" fill="{fill}"/>',
                f'<text x="{x0 + bar + 7:.1f}" y="{y + offset + 14}" font-family="Segoe UI, Arial, sans-serif" font-size="12" fill="#263442">{value:.2f}×</text>',
            ]
    content += [
        '<rect x="760" y="35" width="15" height="10" rx="2" fill="#356f9f"/>',
        '<text x="782" y="44" font-family="Segoe UI, Arial, sans-serif" font-size="12" fill="#536273">vs direct sequential</text>',
        '<rect x="920" y="35" width="15" height="10" rx="2" fill="#6a9a55"/>',
        '<text x="942" y="44" font-family="Segoe UI, Arial, sans-serif" font-size="12" fill="#536273">vs OpenCV API</text>',
        f'<line x1="{x0 + plot_w / max_speedup:.1f}" y1="{top - 10}" x2="{x0 + plot_w / max_speedup:.1f}" y2="{top + row_h * len(data) - 12}" stroke="#253746" stroke-width="1.5" stroke-dasharray="5 4"/>',
    ]
    write_svg(out / "v1.5.0_automatic_speedup.svg", content)


def plot_route_regret(summary: list[dict[str, str]], out: Path) -> None:
    width, height = 1080, 500
    left, top, row_h, plot_w = 230, 95, 58, 720
    max_value = 5.0
    content = svg_header(
        width, height,
        "SmartParallel v1.5 route-selection regret",
        "Distance between the settled route and the fastest eligible forced route; the release limit is 5% or 1 µs.",
    )
    threshold_x = left + plot_w
    content += [
        f'<line x1="{threshold_x}" y1="{top - 12}" x2="{threshold_x}" y2="{top + row_h * len(summary) - 14}" stroke="#b04a4a" stroke-width="2" stroke-dasharray="6 4"/>',
        f'<text x="{threshold_x}" y="{height - 28}" text-anchor="middle" font-family="Segoe UI, Arial, sans-serif" font-size="12" fill="#8e3d3d">5% gate</text>',
    ]
    for tick in range(0, 6):
        x = left + plot_w * tick / max_value
        content += [
            f'<line x1="{x:.1f}" y1="{top - 8}" x2="{x:.1f}" y2="{top + row_h * len(summary) - 14}" stroke="#e5e9ee" stroke-width="1"/>',
            f'<text x="{x:.1f}" y="{height - 28}" text-anchor="middle" font-family="Segoe UI, Arial, sans-serif" font-size="12" fill="#657384">{tick}%</text>',
        ]
    for i, row in enumerate(summary):
        y = top + i * row_h
        value = max(0.0, float(row["route_selection_regret_percent"]))
        bar = plot_w * min(value, max_value) / max_value
        content += [
            f'<text x="{left - 16}" y="{y + 22}" text-anchor="end" font-family="Segoe UI, Arial, sans-serif" font-size="14" fill="#263442">{PRESET_LABELS[row["preset"]]}</text>',
            f'<rect x="{left}" y="{y + 5}" width="{bar:.1f}" height="25" rx="4" fill="#496f88"/>',
            f'<text x="{left + bar + 8:.1f}" y="{y + 23}" font-family="Segoe UI, Arial, sans-serif" font-size="12" fill="#263442">{value:.2f}% · {ROUTE_LABELS.get(row["auto_selected_route"], row["auto_selected_route"])}</text>',
        ]
    write_svg(out / "v1.5.0_route_selection_regret.svg", content)


def plot_native_kernel(summary: list[dict[str, str]], out: Path) -> None:
    width, height = 1080, 500
    left, top, row_h, half_w = 540, 95, 58, 360
    bound = 12.0
    content = svg_header(
        width, height,
        "Native AVX2 kernel versus independent compiler oracle",
        "Negative values mean the SmartParallel Native Sequential kernel was faster; the release gate allows +10% or +0.5 µs.",
    )
    content.append(f'<line x1="{left}" y1="{top - 12}" x2="{left}" y2="{top + row_h * len(summary) - 14}" stroke="#243746" stroke-width="1.5"/>')
    for tick in [-10, -5, 0, 5, 10]:
        x = left + half_w * tick / bound
        content += [
            f'<line x1="{x:.1f}" y1="{top - 8}" x2="{x:.1f}" y2="{top + row_h * len(summary) - 14}" stroke="#e5e9ee" stroke-width="1"/>',
            f'<text x="{x:.1f}" y="{height - 28}" text-anchor="middle" font-family="Segoe UI, Arial, sans-serif" font-size="12" fill="#657384">{tick:+d}%</text>',
        ]
    for i, row in enumerate(summary):
        y = top + i * row_h
        value = float(row["native_kernel_delta_percent"])
        x2 = left + half_w * value / bound
        x = min(left, x2)
        bar = abs(x2 - left)
        fill = "#3f7f63" if value <= 0 else "#b06a4b"
        content += [
            f'<text x="{left - half_w - 20}" y="{y + 22}" font-family="Segoe UI, Arial, sans-serif" font-size="14" fill="#263442">{PRESET_LABELS[row["preset"]]}</text>',
            f'<rect x="{x:.1f}" y="{y + 5}" width="{bar:.1f}" height="25" rx="4" fill="{fill}"/>',
            f'<text x="{(x2 + 8 if value >= 0 else x2 - 8):.1f}" y="{y + 23}" text-anchor="{"start" if value >= 0 else "end"}" font-family="Segoe UI, Arial, sans-serif" font-size="12" fill="#263442">{value:+.2f}%</text>',
        ]
    write_svg(out / "v1.5.0_native_kernel_vs_oracle.svg", content)


def plot_dispatch(summary: list[dict[str, str]], out: Path) -> None:
    width, height = 1080, 500
    left, top, row_h, half_w = 540, 95, 58, 390
    bound = 8.5
    content = svg_header(
        width, height,
        "Stable Auto dispatch overhead",
        "Point estimate and robust 95% interval from adjacent batched ABBA/BAAB comparisons; failure requires the lower bound to exceed +1 µs.",
    )
    for tick in [-8, -4, 0, 1, 4, 8]:
        x = left + half_w * tick / bound
        stroke = "#b04a4a" if tick == 1 else "#e5e9ee"
        width_line = 2 if tick == 1 else 1
        dash = ' stroke-dasharray="6 4"' if tick == 1 else ""
        content += [
            f'<line x1="{x:.1f}" y1="{top - 8}" x2="{x:.1f}" y2="{top + row_h * len(summary) - 14}" stroke="{stroke}" stroke-width="{width_line}"{dash}/>',
            f'<text x="{x:.1f}" y="{height - 28}" text-anchor="middle" font-family="Segoe UI, Arial, sans-serif" font-size="12" fill="#657384">{tick:+d} µs</text>',
        ]
    for i, row in enumerate(summary):
        y = top + i * row_h + 18
        center = float(row["batched_dispatch_overhead_ns"]) / 1000.0
        low = float(row["dispatch_ci_lower_ns"]) / 1000.0
        high = float(row["dispatch_ci_upper_ns"]) / 1000.0
        x_low = left + half_w * max(-bound, min(bound, low)) / bound
        x_high = left + half_w * max(-bound, min(bound, high)) / bound
        x_center = left + half_w * max(-bound, min(bound, center)) / bound
        content += [
            f'<text x="{left - half_w - 20}" y="{y + 5}" font-family="Segoe UI, Arial, sans-serif" font-size="14" fill="#263442">{PRESET_LABELS[row["preset"]]}</text>',
            f'<line x1="{x_low:.1f}" y1="{y}" x2="{x_high:.1f}" y2="{y}" stroke="#61778a" stroke-width="4" stroke-linecap="round"/>',
            f'<circle cx="{x_center:.1f}" cy="{y}" r="5" fill="#234f70"/>',
            f'<text x="{min(width - 160, x_high + 10):.1f}" y="{y + 5}" font-family="Segoe UI, Arial, sans-serif" font-size="12" fill="#263442">{center:+.3f} µs</text>',
        ]
    write_svg(out / "v1.5.0_dispatch_overhead.svg", content)


def plot_adaptation(summary: list[dict[str, str]], learning: list[dict[str, str]], out: Path) -> None:
    width, height = 1120, 525
    left, top, row_h = 210, 92, 64
    by_preset: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in learning:
        by_preset[row["preset"]].append(row)
    content = svg_header(
        width, height,
        "Initial learning and settled execution route",
        "The two 1080p profiles initially preferred OpenCV, detected a changed regime, and switched once to Native Sequential.",
    )
    for i, row in enumerate(summary):
        preset = row["preset"]
        entries = by_preset[preset]
        initial = entries[0]["provisional_route"]
        settled = row["auto_selected_route"]
        switches = int(row["route_switch_count"])
        y = top + i * row_h
        content += [
            f'<text x="{left - 18}" y="{y + 26}" text-anchor="end" font-family="Segoe UI, Arial, sans-serif" font-size="14" fill="#263442">{PRESET_LABELS[preset]}</text>',
            f'<rect x="{left}" y="{y + 3}" width="250" height="38" rx="7" fill="#edf2f6" stroke="#b9c7d2"/>',
            f'<text x="{left + 125}" y="{y + 27}" text-anchor="middle" font-family="Segoe UI, Arial, sans-serif" font-size="13" fill="#263442">Initial: {html.escape(ROUTE_LABELS.get(initial, initial))}</text>',
            f'<line x1="{left + 268}" y1="{y + 22}" x2="{left + 390}" y2="{y + 22}" stroke="#60798c" stroke-width="2"/>',
            f'<polygon points="{left + 390},{y + 22} {left + 378},{y + 15} {left + 378},{y + 29}" fill="#60798c"/>',
            f'<rect x="{left + 410}" y="{y + 3}" width="270" height="38" rx="7" fill="#{"e6f2ea" if switches else "edf2f6"}" stroke="#{"80ad8e" if switches else "b9c7d2"}"/>',
            f'<text x="{left + 545}" y="{y + 27}" text-anchor="middle" font-family="Segoe UI, Arial, sans-serif" font-size="13" fill="#263442">Settled: {html.escape(ROUTE_LABELS.get(settled, settled))}</text>',
            f'<text x="{left + 715}" y="{y + 27}" font-family="Segoe UI, Arial, sans-serif" font-size="12" fill="#536273">{switches} runtime switch{"es" if switches != 1 else ""}</text>',
        ]
    write_svg(out / "v1.5.0_adaptive_route_map.svg", content)


def main() -> int:
    args = parse_args()
    summary = read_csv(args.summary_csv)
    raw = read_csv(args.raw_csv)
    learning = read_csv(args.learning_csv)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    medians = steady_medians(raw)
    plot_speedups(summary, medians, args.output_dir)
    plot_route_regret(summary, args.output_dir)
    plot_native_kernel(summary, args.output_dir)
    plot_dispatch(summary, args.output_dir)
    plot_adaptation(summary, learning, args.output_dir)
    print(f"Generated v1.5 benchmark plots in {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
