#!/usr/bin/env python3
"""Validate and analyze SmartParallel v1.5 adaptive execution-route results."""

from __future__ import annotations

import argparse
import csv
import hashlib
import html
import math
import statistics
from collections import defaultdict
from pathlib import Path

REQUIRED_COLUMNS = {
    "schema_version", "benchmark_version", "operation", "preset",
    "implementation", "phase", "repetition_index", "measurement_ordinal",
    "width", "height", "stride_bytes", "duration_ns", "batch_total_ns",
    "batch_iterations", "checksum", "mismatch_count", "requested_route",
    "selected_route", "worker_budget", "participant_count", "chunk_count",
    "learned_route", "probe", "exploration_probe", "holdout_probe",
    "revalidation_probe", "correctness_pass", "backend_authentication_pass",
    "requested_repetitions", "source_address", "destination_address",
    "source_alignment", "destination_alignment", "shared_destination_id",
    "learning_invocations", "deployment_invocations", "route_switch_count",
    "pair_order", "pair_position", "native_kernel", "drift_probe",
}

LEARNING_COLUMNS = {
    "schema_version", "preset", "stable", "stable_route", "provisional_route",
    "holdout_active", "revalidation_active", "drift_detected",
    "verification_failures", "route_switch_count", "drift_strikes",
    "revalidation_challenger", "training_baseline_ms", "current_baseline_ms",
    "last_revalidation_stable_ms", "last_revalidation_challenger_ms",
    "route", "median_ms", "mad_ms", "minimum_ms", "maximum_ms",
    "current_median_ms", "sample_count", "current_sample_count",
    "warmup_count", "holdout_sample_count", "active",
}

BASE_IMPLEMENTATIONS = {
    "opencv_api", "direct_sequential", "smart_auto",
    "smart_native_sequential", "smart_native_thread_pool",
    "smart_native_static_thread", "smart_opencv",
}
REQUIRED_PRESETS = [
    "tiny_320x240", "small_640x480", "medium_1920x1080",
    "medium_1920x1080_roi", "large_3840x2160", "very_large_7680x4320",
]
FORCED_ROUTE_EXPECTATIONS = {
    "smart_native_sequential": "native_sequential",
    "smart_native_thread_pool": "native_thread_pool",
    "smart_native_static_thread": "native_static_thread",
    "smart_native_one_tbb": "native_one_tbb",
    "smart_opencv": "opencv",
}
ROUTE_TO_IMPLEMENTATION = {
    "native_sequential": "smart_native_sequential",
    "native_thread_pool": "smart_native_thread_pool",
    "native_one_tbb": "smart_native_one_tbb",
    "opencv": "smart_opencv",
}
ELIGIBLE_AUTO_IMPLEMENTATIONS = {
    "smart_native_sequential", "smart_native_thread_pool",
    "smart_native_one_tbb", "smart_opencv",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("raw_csv", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--equivalence-percent", type=float, default=5.0)
    parser.add_argument("--absolute-tolerance-ns", type=int, default=1_000)
    parser.add_argument("--native-relative-percent", type=float, default=10.0)
    parser.add_argument("--native-absolute-tolerance-ns", type=int, default=500)
    parser.add_argument("--dispatch-overhead-ns", type=int, default=1_000)
    parser.add_argument("--allow-missing-onetbb", action="store_true")
    return parser.parse_args()


def as_bool(value: str) -> bool:
    return value.strip().lower() in {"1", "true", "yes"}


def observed_median(values: list[int | float]) -> float:
    if not values:
        raise ValueError("Cannot compute a median of an empty sample set")
    return float(statistics.median(values))


def median_absolute_deviation(values: list[int | float], center: float) -> float:
    return observed_median([abs(float(value) - center) for value in values])


def read_csv(path: Path, required: set[str]) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        missing = required - set(reader.fieldnames or [])
        if missing:
            raise ValueError(f"{path.name} is missing columns: {sorted(missing)}")
        rows = list(reader)
    if not rows:
        raise ValueError(f"{path.name} is empty")
    return rows


def unique_in_order(values: list[str]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for value in values:
        if value not in seen:
            seen.add(value)
            result.append(value)
    return result


def confidence_interval_median(values: list[float]) -> tuple[float, float, float]:
    center = observed_median(values)
    mad = median_absolute_deviation(values, center)
    if len(values) <= 1:
        return center, center, center
    half = 1.96 * 1.4826 * mad / math.sqrt(len(values))
    return center, center - half, center + half


def validate_learning(path: Path, presets: list[str]) -> dict[str, list[dict[str, str]]]:
    rows = read_csv(path, LEARNING_COLUMNS)
    if {row["schema_version"] for row in rows} != {"2"}:
        raise ValueError("Unexpected learning telemetry schema")
    grouped: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[row["preset"]].append(row)
    if list(grouped) != presets:
        raise ValueError("Learning telemetry preset matrix mismatch")
    for preset, entries in grouped.items():
        stable_routes = {row["stable_route"] for row in entries}
        if len(stable_routes) != 1 or any(not as_bool(row["stable"]) for row in entries):
            raise ValueError(f"{preset}: unstable learning telemetry")
        if any(as_bool(row["holdout_active"]) for row in entries):
            raise ValueError(f"{preset}: holdout remained active after stabilization")
        if any(as_bool(row["revalidation_active"]) for row in entries):
            raise ValueError(f"{preset}: current-context revalidation remained active")
        if any(int(row["sample_count"]) <= 0 for row in entries):
            raise ValueError(f"{preset}: missing measured training samples")
        if any(int(row["warmup_count"]) < 2 for row in entries):
            raise ValueError(f"{preset}: routes were not primed twice")
        if not any(int(row["holdout_sample_count"]) >= 2 for row in entries):
            raise ValueError(f"{preset}: independent holdout verification was not recorded")
    return grouped


def validate(rows: list[dict[str, str]], allow_missing_onetbb: bool) -> tuple[int, list[str], list[str]]:
    if {row["schema_version"] for row in rows} != {"6"}:
        raise ValueError("Publication data must use raw schema version 6")
    if {row["benchmark_version"] for row in rows} != {"1.5.0"}:
        raise ValueError("Unexpected benchmark version")
    if {row["operation"] for row in rows} != {"threshold_u8_binary"}:
        raise ValueError("Unexpected operation matrix")
    native_kernels = {row["native_kernel"] for row in rows}
    if len(native_kernels) != 1 or "" in native_kernels:
        raise ValueError(f"Inconsistent native kernel identity: {sorted(native_kernels)}")

    repetition_values = {int(row["requested_repetitions"]) for row in rows}
    if len(repetition_values) != 1:
        raise ValueError("Raw rows disagree on repetition count")
    repetitions = repetition_values.pop()
    if repetitions <= 0 or repetitions % 2 == 0:
        raise ValueError("Publication repetition count must be positive and odd")

    if any(not as_bool(row["correctness_pass"]) or int(row["mismatch_count"]) != 0
           for row in rows):
        raise ValueError("Correctness failed")
    if any(not as_bool(row["backend_authentication_pass"]) for row in rows):
        raise ValueError("Backend authentication failed")

    presets = unique_in_order([row["preset"] for row in rows])
    implementations = unique_in_order([row["implementation"] for row in rows])
    if presets != REQUIRED_PRESETS:
        raise ValueError(f"Preset matrix mismatch: {presets}")
    missing = BASE_IMPLEMENTATIONS - set(implementations)
    if missing:
        raise ValueError(f"Missing required implementations: {sorted(missing)}")
    if not allow_missing_onetbb and "smart_native_one_tbb" not in implementations:
        raise ValueError("Publication matrix is missing smart_native_one_tbb")

    for implementation, route in FORCED_ROUTE_EXPECTATIONS.items():
        invalid = [row for row in rows if row["implementation"] == implementation
                   and row["selected_route"] != route]
        if invalid:
            raise ValueError(f"{implementation} did not authenticate {route}")

    for preset in presets:
        preset_rows = [row for row in rows if row["preset"] == preset]
        if len({row["checksum"] for row in preset_rows}) != 1:
            raise ValueError(f"{preset}: inconsistent checksums")
        if len({row["source_address"] for row in preset_rows}) != 1 \
                or len({row["destination_address"] for row in preset_rows}) != 1:
            raise ValueError(f"{preset}: routes did not use identical allocations")
        if {row["shared_destination_id"] for row in preset_rows} != {"shared_per_preset"}:
            raise ValueError(f"{preset}: invalid shared destination declaration")
        if len({row["source_alignment"] for row in preset_rows}) != 1 \
                or len({row["destination_alignment"] for row in preset_rows}) != 1:
            raise ValueError(f"{preset}: alignment changed across routes")

        cold = [row for row in preset_rows if row["phase"] == "cold"]
        if len(cold) != 1 or cold[0]["implementation"] != "smart_auto":
            raise ValueError(f"{preset}: expected one cold Auto sample")

        steady = [row for row in preset_rows if row["phase"] == "steady_state"]
        grouped: dict[str, list[dict[str, str]]] = defaultdict(list)
        for row in steady:
            grouped[row["implementation"]].append(row)
        for implementation in implementations:
            if len(grouped[implementation]) != repetitions:
                raise ValueError(
                    f"{preset}/{implementation}: expected {repetitions} steady samples, "
                    f"got {len(grouped[implementation])}"
                )
        auto_rows = grouped["smart_auto"]
        if any(as_bool(row["probe"]) or as_bool(row["exploration_probe"])
               or as_bool(row["holdout_probe"]) or as_bool(row["revalidation_probe"])
               or as_bool(row["drift_probe"]) for row in auto_rows):
            raise ValueError(f"{preset}: steady Auto data contains probes")
        if any(not as_bool(row["learned_route"]) for row in auto_rows):
            raise ValueError(f"{preset}: steady Auto rows are not learned")
        routes = {row["selected_route"] for row in auto_rows}
        if len(routes) != 1 or not routes <= set(ROUTE_TO_IMPLEMENTATION):
            raise ValueError(f"{preset}: unstable or invalid Auto route")
        learning_counts = {int(row["learning_invocations"]) for row in steady}
        deployment_counts = {int(row["deployment_invocations"]) for row in steady}
        switch_counts = {int(row["route_switch_count"]) for row in auto_rows}
        if len(learning_counts) != 1 or next(iter(learning_counts)) <= 0:
            raise ValueError(f"{preset}: invalid learning invocation metadata")
        if len(deployment_counts) != 1 or next(iter(deployment_counts)) <= 0:
            raise ValueError(f"{preset}: invalid deployment-settling metadata")
        if len(switch_counts) != 1:
            raise ValueError(f"{preset}: inconsistent route-switch metadata")

        # Balanced order check: every implementation occupies each position nearly equally.
        positions: dict[str, list[int]] = defaultdict(list)
        by_rep: dict[int, list[dict[str, str]]] = defaultdict(list)
        for row in steady:
            by_rep[int(row["repetition_index"])].append(row)
        for repetition in range(repetitions):
            block = sorted(by_rep[repetition], key=lambda row: int(row["measurement_ordinal"]))
            if len(block) != len(implementations) or {row["implementation"] for row in block} != set(implementations):
                raise ValueError(f"{preset}: malformed balanced block {repetition}")
            for position, row in enumerate(block):
                positions[row["implementation"]].append(position)
        for implementation, observed in positions.items():
            counts = [observed.count(position) for position in range(len(implementations))]
            if max(counts) - min(counts) > 1:
                raise ValueError(f"{preset}/{implementation}: unbalanced measurement positions")

        batches = [row for row in preset_rows if row["phase"] == "dispatch_batch"]
        if len(batches) != repetitions * 4:
            raise ValueError(f"{preset}: expected {repetitions * 4} dispatch batch rows")
        selected_impl = ROUTE_TO_IMPLEMENTATION[next(iter(routes))]
        by_pair: dict[int, list[dict[str, str]]] = defaultdict(list)
        for row in batches:
            by_pair[int(row["repetition_index"])].append(row)
        for repetition in range(repetitions):
            block = sorted(by_pair[repetition], key=lambda row: int(row["pair_position"]))
            if [int(row["pair_position"]) for row in block] != [1, 2, 3, 4]:
                raise ValueError(f"{preset}: malformed dispatch block {repetition}")
            expected_order = ("auto_forced_forced_auto" if repetition % 2 == 0
                              else "forced_auto_auto_forced")
            if {row["pair_order"] for row in block} != {expected_order}:
                raise ValueError(f"{preset}: incorrect dispatch block metadata")
            expected_impls = (["smart_auto", selected_impl, selected_impl, "smart_auto"]
                              if repetition % 2 == 0
                              else [selected_impl, "smart_auto", "smart_auto", selected_impl])
            if [row["implementation"] for row in block] != expected_impls:
                raise ValueError(f"{preset}: incorrect ABBA/BAAB sequence")
            iterations = {int(row["batch_iterations"]) for row in block}
            if len(iterations) != 1 or next(iter(iterations)) <= 0:
                raise ValueError(f"{preset}: invalid batch iteration metadata")
            for row in block:
                if int(row["batch_total_ns"]) <= 0:
                    raise ValueError(f"{preset}: invalid batch duration")
                if row["implementation"] == "smart_auto" and (
                    as_bool(row["probe"]) or as_bool(row["drift_probe"])
                    or not as_bool(row["learned_route"])
                ):
                    raise ValueError(f"{preset}: dispatch batch Auto route was not stable")
    return repetitions, presets, implementations


def summarize(rows: list[dict[str, str]], presets: list[str], implementations: list[str],
              learning: dict[str, list[dict[str, str]]], args: argparse.Namespace) -> list[dict[str, object]]:
    summaries: list[dict[str, object]] = []
    for preset in presets:
        steady = [row for row in rows if row["preset"] == preset and row["phase"] == "steady_state"]
        grouped: dict[str, list[int]] = defaultdict(list)
        metadata: dict[str, dict[str, str]] = {}
        for row in steady:
            grouped[row["implementation"]].append(int(row["duration_ns"]))
            metadata[row["implementation"]] = row
        medians = {implementation: int(observed_median(values))
                   for implementation, values in grouped.items()}
        eligible = [implementation for implementation in implementations
                    if implementation in ELIGIBLE_AUTO_IMPLEMENTATIONS]
        fastest = min(eligible, key=lambda implementation: medians[implementation])
        fastest_ns = medians[fastest]
        auto_meta = metadata["smart_auto"]
        selected_route = auto_meta["selected_route"]
        selected_impl = ROUTE_TO_IMPLEMENTATION[selected_route]
        selected_ns = medians[selected_impl]
        route_regret_ns = selected_ns - fastest_ns
        route_regret_percent = (selected_ns / fastest_ns - 1.0) * 100.0
        route_pass = route_regret_percent <= args.equivalence_percent \
            or route_regret_ns <= args.absolute_tolerance_ns

        batch_rows = [row for row in rows if row["preset"] == preset
                      and row["phase"] == "dispatch_batch"]
        by_rep: dict[int, list[dict[str, str]]] = defaultdict(list)
        for row in batch_rows:
            by_rep[int(row["repetition_index"])].append(row)
        deltas: list[float] = []
        for block in by_rep.values():
            auto_total = sum(int(row["batch_total_ns"]) for row in block
                             if row["implementation"] == "smart_auto")
            forced_total = sum(int(row["batch_total_ns"]) for row in block
                               if row["implementation"] == selected_impl)
            auto_iterations = sum(int(row["batch_iterations"]) for row in block
                                  if row["implementation"] == "smart_auto")
            forced_iterations = sum(int(row["batch_iterations"]) for row in block
                                    if row["implementation"] == selected_impl)
            deltas.append(auto_total / auto_iterations - forced_total / forced_iterations)
        dispatch_center, dispatch_lower, dispatch_upper = confidence_interval_median(deltas)
        # Only fail when the lower confidence bound confidently exceeds the gate.
        dispatch_pass = dispatch_lower <= args.dispatch_overhead_ns
        dispatch_status = ("pass" if dispatch_upper <= args.dispatch_overhead_ns
                           else "fail" if dispatch_lower > args.dispatch_overhead_ns
                           else "inconclusive-pass")

        direct_ns = medians["direct_sequential"]
        native_ns = medians["smart_native_sequential"]
        native_delta_ns = native_ns - direct_ns
        native_delta_percent = (native_ns / direct_ns - 1.0) * 100.0
        native_pass = native_delta_percent <= args.native_relative_percent \
            or native_delta_ns <= args.native_absolute_tolerance_ns

        auto_ns = medians["smart_auto"]
        training = learning[preset]
        learned_entry = next(row for row in training if row["route"] == selected_route)
        overall = route_pass and dispatch_pass and native_pass
        summaries.append({
            "benchmark_version": "1.5.0",
            "preset": preset,
            "width": int(auto_meta["width"]),
            "height": int(auto_meta["height"]),
            "stride_bytes": int(auto_meta["stride_bytes"]),
            "source_alignment": int(auto_meta["source_alignment"]),
            "destination_alignment": int(auto_meta["destination_alignment"]),
            "learning_invocations": int(auto_meta["learning_invocations"]),
            "deployment_invocations": int(auto_meta["deployment_invocations"]),
            "route_switch_count": int(auto_meta["route_switch_count"]),
            "drift_detected": int(as_bool(learned_entry["drift_detected"])),
            "training_baseline_ns": int(float(learned_entry["training_baseline_ms"]) * 1_000_000),
            "current_baseline_ns": int(float(learned_entry["current_baseline_ms"]) * 1_000_000),
            "last_revalidation_stable_ns": int(float(learned_entry["last_revalidation_stable_ms"]) * 1_000_000),
            "last_revalidation_challenger_ns": int(float(learned_entry["last_revalidation_challenger_ms"]) * 1_000_000),
            "learning_verification_failures": int(learned_entry["verification_failures"]),
            "learned_route_training_median_ns": int(float(learned_entry["median_ms"]) * 1_000_000),
            "learned_route_training_mad_ns": int(float(learned_entry["mad_ms"]) * 1_000_000),
            "learned_route_training_samples": int(learned_entry["sample_count"]),
            "learned_route_holdout_samples": int(learned_entry["holdout_sample_count"]),
            "native_kernel": auto_meta["native_kernel"],
            "auto_selected_route": selected_route,
            "auto_median_ns": auto_ns,
            "selected_forced_implementation": selected_impl,
            "selected_forced_median_ns": selected_ns,
            "fastest_eligible_implementation": fastest,
            "fastest_eligible_median_ns": fastest_ns,
            "route_selection_regret_ns": route_regret_ns,
            "route_selection_regret_percent": route_regret_percent,
            "route_selection_pass": int(route_pass),
            "batched_dispatch_overhead_ns": int(round(dispatch_center)),
            "dispatch_ci_lower_ns": int(round(dispatch_lower)),
            "dispatch_ci_upper_ns": int(round(dispatch_upper)),
            "dispatch_status": dispatch_status,
            "dispatch_overhead_pass": int(dispatch_pass),
            "direct_sequential_median_ns": direct_ns,
            "native_sequential_median_ns": native_ns,
            "native_kernel_delta_ns": native_delta_ns,
            "native_kernel_delta_percent": native_delta_percent,
            "native_kernel_pass": int(native_pass),
            "end_to_end_auto_regret_ns": auto_ns - fastest_ns,
            "end_to_end_auto_regret_percent": (auto_ns / fastest_ns - 1.0) * 100.0,
            "overall_pass": int(overall),
            "auto_mad_ns": int(median_absolute_deviation(grouped["smart_auto"], auto_ns)),
        })
    return summaries


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def write_report(path: Path, raw_path: Path, rows: list[dict[str, object]], repetitions: int,
                 args: argparse.Namespace) -> None:
    raw_sha = hashlib.sha256(raw_path.read_bytes()).hexdigest()
    learning_path = raw_path.parent / "v1.5.0_adaptive_routes_learning.csv"
    learning_sha = hashlib.sha256(learning_path.read_bytes()).hexdigest()
    passed = sum(int(row["overall_pass"]) for row in rows)
    route_passed = sum(int(row["route_selection_pass"]) for row in rows)
    dispatch_passed = sum(int(row["dispatch_overhead_pass"]) for row in rows)
    native_passed = sum(int(row["native_kernel_pass"]) for row in rows)
    lines = [
        "# SmartParallel v1.5 Adaptive Execution Routes",
        "",
        "This report validates balanced initial learning, sparse drift sentinels, current-context ABBA revalidation, deployment settling, balanced steady-state route ordering, and batched adjacent ABBA/BAAB dispatch measurements.",
        "",
        f"- Steady-state repetitions: **{repetitions}**",
        f"- Route equivalence: **{args.equivalence_percent:.1f}% or {args.absolute_tolerance_ns / 1000:.1f} µs**",
        f"- Native oracle gate: **{args.native_relative_percent:.1f}% or {args.native_absolute_tolerance_ns / 1000:.1f} µs**",
        f"- Dispatch gate: **{args.dispatch_overhead_ns / 1000:.1f} µs; failure only when the lower 95% robust bound exceeds the gate**",
        f"- Native kernel: **{rows[0]['native_kernel']}**",
        f"- Raw CSV SHA-256: `{raw_sha}`",
        f"- Learning CSV SHA-256: `{learning_sha}`",
        "",
        "## Proof gates",
        "",
        "| Preset | Settled route | Switches | Fastest route | Route regret | Batched overhead (95% robust interval) | Native vs oracle | Training samples + holdout | Overall |",
        "|---|---|---:|---|---:|---:|---:|---:|---|",
    ]
    for row in rows:
        lines.append(
            f"| {row['preset']} | {row['auto_selected_route']} | "
            f"{row['route_switch_count']} | {row['fastest_eligible_implementation']} | "
            f"{float(row['route_selection_regret_percent']):+.2f}% | "
            f"{int(row['batched_dispatch_overhead_ns']) / 1000:+.3f} µs "
            f"[{int(row['dispatch_ci_lower_ns']) / 1000:+.3f}, "
            f"{int(row['dispatch_ci_upper_ns']) / 1000:+.3f}] ({row['dispatch_status']}) | "
            f"{float(row['native_kernel_delta_percent']):+.2f}% | "
            f"{row['learned_route_training_samples']} + {row['learned_route_holdout_samples']} | "
            f"{'PASS' if int(row['overall_pass']) else 'FAIL'} |"
        )
    lines += [
        "", "## Verdict", "",
        f"- Route selection: **{route_passed}/{len(rows)}** presets passed.",
        f"- Batched stable hot dispatch: **{dispatch_passed}/{len(rows)}** presets passed.",
        f"- Native kernel versus independent oracle: **{native_passed}/{len(rows)}** presets passed.",
        f"- Combined release gate: **{passed}/{len(rows)}** presets passed.", "",
    ]
    lines.append("The v1.5 threshold vertical slice passed all proof gates on this machine."
                 if passed == len(rows)
                 else "One or more proof gates remain outside the release target.")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_svg(path: Path, rows: list[dict[str, object]]) -> None:
    width, left, top, row_h = 1050, 260, 70, 62
    height = top + row_h * len(rows) + 70
    max_value = max(10.0, max(max(0.0, float(row["end_to_end_auto_regret_percent"])) for row in rows))
    plot_width = 680
    content = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<text x="35" y="38" font-family="sans-serif" font-size="24" font-weight="700">SmartParallel v1.5 balanced-learning Auto regret</text>',
    ]
    for index, row in enumerate(rows):
        y = top + index * row_h
        value = max(0.0, float(row["end_to_end_auto_regret_percent"]))
        bar = plot_width * value / max_value
        content += [
            f'<text x="{left - 12}" y="{y + 22}" text-anchor="end" font-family="sans-serif" font-size="14">{html.escape(str(row["preset"]))}</text>',
            f'<rect x="{left}" y="{y + 5}" width="{bar:.1f}" height="24" rx="4" fill="#496a81"/>',
            f'<text x="{left + bar + 8:.1f}" y="{y + 23}" font-family="sans-serif" font-size="13">{value:.2f}% · {html.escape(str(row["auto_selected_route"]))}</text>',
        ]
    content.append('</svg>')
    path.write_text("\n".join(content), encoding="utf-8")


def main() -> int:
    args = parse_args()
    rows = read_csv(args.raw_csv, REQUIRED_COLUMNS)
    repetitions, presets, implementations = validate(rows, args.allow_missing_onetbb)
    learning_path = args.raw_csv.parent / "v1.5.0_adaptive_routes_learning.csv"
    learning = validate_learning(learning_path, presets)
    summary = summarize(rows, presets, implementations, learning, args)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    summary_path = args.output_dir / "v1.5.0_adaptive_routes.csv"
    report_path = args.output_dir / "v1.5.0_adaptive_routes_report.md"
    write_csv(summary_path, summary)
    write_report(report_path, args.raw_csv, summary, repetitions, args)
    write_svg(args.output_dir / "v1.5.0_auto_regret.svg", summary)
    print(f"Validated {len(rows)} raw samples")
    print(f"Summary: {summary_path}")
    print(f"Report: {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
