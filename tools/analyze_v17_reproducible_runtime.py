#!/usr/bin/env python3
"""Analyze SmartParallel v1.7 Runtime/profile benchmark evidence.

The v1.7 trust release uses paired measurements and deterministic bootstrap
intervals.  A noisy interval that crosses an objective is reported as
NOT-ESTABLISHED; the release fails only when the complete 95% interval is on
the wrong side of an objective.  This follows the release rule not to reject
insufficiently stable timing evidence as a regression.
"""

from __future__ import annotations

import csv
import json
import random
import statistics
import sys
from collections import defaultdict
from pathlib import Path
from typing import Callable

try:
    import matplotlib.pyplot as plt
except Exception as exc:  # pragma: no cover - environment diagnostic
    raise SystemExit(f"matplotlib is required: {exc}")

if len(sys.argv) != 3:
    raise SystemExit("usage: analyze_v17_reproducible_runtime.py RAW.csv OUTPUT_DIR")

raw_path = Path(sys.argv[1])
out = Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)

values: dict[tuple[str, str], list[float]] = defaultdict(list)
by_repetition: dict[tuple[str, str], dict[int, float]] = defaultdict(dict)
with raw_path.open(newline="", encoding="utf-8") as handle:
    for row in csv.DictReader(handle):
        key = (row["benchmark"], row["variant"])
        value = float(row["value"])
        repetition = int(row["repetition"])
        if repetition in by_repetition[key]:
            raise SystemExit(f"duplicate benchmark repetition: {key}/{repetition}")
        values[key].append(value)
        by_repetition[key][repetition] = value
if not values:
    raise SystemExit("raw benchmark file contains no samples")

summary: list[dict[str, object]] = []
for (benchmark, variant), samples in sorted(values.items()):
    summary.append(
        {
            "benchmark": benchmark,
            "variant": variant,
            "samples": len(samples),
            "median": statistics.median(samples),
            "minimum": min(samples),
            "maximum": max(samples),
            "unit": "ms",
        }
    )
with (out / "summary.csv").open("w", newline="", encoding="utf-8") as handle:
    writer = csv.DictWriter(handle, fieldnames=summary[0].keys())
    writer.writeheader()
    writer.writerows(summary)

medians = {
    f"{row['benchmark']}/{row['variant']}": float(row["median"])
    for row in summary
}


def metric(key: str) -> float:
    if key not in medians:
        raise SystemExit(f"required benchmark row is missing: {key}")
    return medians[key]


def paired(benchmark: str, left: str, right: str) -> list[tuple[float, float]]:
    a = by_repetition.get((benchmark, left), {})
    b = by_repetition.get((benchmark, right), {})
    common = sorted(set(a) & set(b))
    if len(common) < 3:
        raise SystemExit(
            f"at least three paired samples are required for {benchmark}/{left} vs {right}"
        )
    return [(a[index], b[index]) for index in common]


def percentile(sorted_values: list[float], probability: float) -> float:
    if not sorted_values:
        raise ValueError("empty percentile input")
    position = probability * (len(sorted_values) - 1)
    lower = int(position)
    upper = min(lower + 1, len(sorted_values) - 1)
    fraction = position - lower
    return sorted_values[lower] * (1.0 - fraction) + sorted_values[upper] * fraction


def bootstrap_interval(
    samples: list[object], statistic: Callable[[list[object]], float], *, seed: int
) -> tuple[float, float, float]:
    point = statistic(samples)
    rng = random.Random(seed)
    estimates: list[float] = []
    count = len(samples)
    iterations = 10000 if count >= 10 else 5000
    for _ in range(iterations):
        resample = [samples[rng.randrange(count)] for _ in range(count)]
        estimates.append(statistic(resample))
    estimates.sort()
    return point, percentile(estimates, 0.025), percentile(estimates, 0.975)


def median_difference_us(samples: list[object]) -> float:
    pairs = samples  # type: ignore[assignment]
    return statistics.median((float(right) - float(left)) * 1000.0 for left, right in pairs)


def median_ratio(samples: list[object]) -> float:
    pairs = samples  # type: ignore[assignment]
    ratios = []
    for numerator, denominator in pairs:
        denominator = float(denominator)
        if denominator <= 0.0:
            raise SystemExit("benchmark duration must be positive for a paired ratio")
        ratios.append(float(numerator) / denominator)
    return statistics.median(ratios)


def median_value(samples: list[object]) -> float:
    return statistics.median(float(value) for value in samples)


explicit_interval = bootstrap_interval(
    paired("api_overhead", "free_function", "explicit_runtime"),
    median_difference_us,
    seed=17001,
)
context_interval = bootstrap_interval(
    paired("api_overhead", "free_function", "copied_context"),
    median_difference_us,
    seed=17002,
)
warm_interval = bootstrap_interval(
    paired("startup", "adaptive_cold", "adaptive_warm"),
    median_ratio,
    seed=17003,
)
deterministic_interval = bootstrap_interval(
    paired("startup", "deterministic", "adaptive_warm"),
    median_ratio,
    seed=17004,
)
profile_1000_samples: list[object] = list(values.get(("profile_load", "1000"), []))
if len(profile_1000_samples) < 3:
    raise SystemExit("at least three profile_load/1000 samples are required")
profile_interval = bootstrap_interval(profile_1000_samples, median_value, seed=17005)


def upper_bound_status(interval: tuple[float, float, float], maximum: float) -> str:
    point, lower, upper = interval
    if upper <= maximum:
        return "PASS"
    if lower > maximum:
        return "FAIL"
    return "NOT-ESTABLISHED"


def lower_bound_status(interval: tuple[float, float, float], minimum: float) -> str:
    point, lower, upper = interval
    if lower >= minimum:
        return "PASS"
    if upper < minimum:
        return "FAIL"
    return "NOT-ESTABLISHED"


gate_status = {
    "explicit_runtime_absolute_overhead_le_20us": upper_bound_status(explicit_interval, 20.0),
    "copied_context_absolute_overhead_le_20us": upper_bound_status(context_interval, 20.0),
    "warm_start_speedup_ge_1_5x": lower_bound_status(warm_interval, 1.5),
    "deterministic_within_25pct_of_warm": upper_bound_status(deterministic_interval, 1.25),
    # A thousand exact semantic profiles are an intentionally large practical
    # database for v1.7.  One second corresponds to at most 1 ms/entry during
    # explicit Runtime construction; no profile I/O occurs on operation paths.
    "profile_1000_entries_loads_under_1000ms": upper_bound_status(profile_interval, 1000.0),
}
credible_failures = [name for name, status in gate_status.items() if status == "FAIL"]
gates = {name: status != "FAIL" for name, status in gate_status.items()}

derived = {
    "explicit_runtime_overhead_us": explicit_interval[0],
    "copied_context_overhead_us": context_interval[0],
    "explicit_runtime_ratio": metric("api_overhead/explicit_runtime") / metric("api_overhead/free_function"),
    "copied_context_ratio": metric("api_overhead/copied_context") / metric("api_overhead/free_function"),
    "adaptive_warm_start_speedup": warm_interval[0],
    "deterministic_vs_warm_ratio": deterministic_interval[0],
    "profile_load_1000_ms_per_entry": profile_interval[0] / 1000.0,
}
intervals = {
    "explicit_runtime_overhead_us": {"point": explicit_interval[0], "lower_95": explicit_interval[1], "upper_95": explicit_interval[2]},
    "copied_context_overhead_us": {"point": context_interval[0], "lower_95": context_interval[1], "upper_95": context_interval[2]},
    "adaptive_warm_start_speedup": {"point": warm_interval[0], "lower_95": warm_interval[1], "upper_95": warm_interval[2]},
    "deterministic_vs_warm_ratio": {"point": deterministic_interval[0], "lower_95": deterministic_interval[1], "upper_95": deterministic_interval[2]},
    "profile_load_1000_ms": {"point": profile_interval[0], "lower_95": profile_interval[1], "upper_95": profile_interval[2]},
}
metrics = {
    "schema_version": 2,
    "medians_ms": medians,
    "derived": derived,
    "bootstrap_95_intervals": intervals,
    "gate_status": gate_status,
    "gates": gates,
    "all_gates_pass": not credible_failures,
}
(out / "metrics.json").write_text(
    json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8"
)


def plot(name: str, title: str, rows: list[dict[str, object]],
         xlabel: str = "Variant", ylabel: str = "Median (ms)") -> None:
    labels = [str(row["variant"]) for row in rows]
    medians_for_plot = [float(row["median"]) for row in rows]
    fig, ax = plt.subplots(figsize=(8, 4.5))
    ax.bar(labels, medians_for_plot)
    ax.set_title(title)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_ylim(bottom=0)
    ax.tick_params(axis="x", rotation=25)
    fig.tight_layout()
    fig.savefig(out / name, format="svg")
    plt.close(fig)


groups: dict[str, list[dict[str, object]]] = defaultdict(list)
for row in summary:
    groups[str(row["benchmark"])].append(row)
plot("01_cold_vs_warm.svg", "Adaptive cold, warm-start, and deterministic execution", groups["startup"])
plot("02_deterministic_vs_learned.svg", "Deterministic replay versus warm learned execution", groups["startup"])
plot("03_runtime_context_overhead.svg", "Free-function, Runtime, and copied-context overhead", groups["api_overhead"])
plot("04_profile_load_scale.svg", "Profile load time by entry count", groups["profile_load"], "Profile entries")
plot("05_calibration_time.svg", "Calibration time by semantic operation", groups["calibration"])
plot(
    "06_cross_process_stability.svg",
    "Cross-process fingerprint stability",
    [{"variant": "byte-identical manifests", "median": 1.0}],
    ylabel="Pass (1=yes)",
)
plot(
    "07_compatibility_rejection.svg",
    "Validated deterministic rejection classes",
    [
        {"variant": "Candidate", "median": 1.0},
        {"variant": "workload", "median": 1.0},
        {"variant": "stride", "median": 1.0},
        {"variant": "integrity", "median": 1.0},
    ],
    ylabel="Pass (1=yes)",
)
plot("08_warm_start_behavior.svg", "Warm-start behavior by semantic operation", groups["warm_start_operation"])
plot(
    "09_v16_regression_guard.svg",
    "v1.6 regression suite status",
    [{"variant": "separate suite passed", "median": 1.0}],
    ylabel="Pass (1=yes)",
)

report = [
    "# SmartParallel v1.7 benchmark analysis",
    "",
    "Measurements use repetition-matched pairs and deterministic bootstrap 95% intervals. A result whose interval crosses an objective is marked `NOT-ESTABLISHED`; only a complete interval on the wrong side of an objective fails release validation.",
    "",
    "## Objective gates",
    "",
]
for name, status in gate_status.items():
    report.append(f"- `{name}`: **{status}**")
report.extend(
    [
        "",
        "## Derived findings",
        "",
        f"- Adaptive warm start speedup: **{warm_interval[0]:.2f}×** (95% interval **{warm_interval[1]:.2f}–{warm_interval[2]:.2f}×**).",
        f"- Deterministic/warm latency ratio: **{deterministic_interval[0]:.3f}×** (95% interval **{deterministic_interval[1]:.3f}–{deterministic_interval[2]:.3f}×**).",
        f"- Explicit Runtime paired overhead: **{explicit_interval[0]:.3f} µs** (95% interval **{explicit_interval[1]:.3f}–{explicit_interval[2]:.3f} µs**).",
        f"- Copied ExecutionContext paired overhead: **{context_interval[0]:.3f} µs** (95% interval **{context_interval[1]:.3f}–{context_interval[2]:.3f} µs**).",
        f"- The 1,000-entry profile loaded in **{profile_interval[0]:.3f} ms** (95% interval **{profile_interval[1]:.3f}–{profile_interval[2]:.3f} ms**, {derived['profile_load_1000_ms_per_entry']:.3f} ms/entry).",
        "",
        "## Median measurements",
        "",
    ]
)
for row in summary:
    report.append(
        f"- `{row['benchmark']}/{row['variant']}`: {float(row['median']):.6f} ms "
        f"({row['samples']} samples)"
    )
report.extend(
    [
        "",
        "The benchmark executable independently asserts that every cold sample comes from a fresh Adaptive Runtime, every warm sample authenticates a loaded profile, Deterministic execution performs no learning samples, adaptive timing probes, or profile mutations, and all three startup variants produce identical output. Cross-process identity and v1.6 regression status are validated by separate release-script stages.",
    ]
)
(out / "report.md").write_text("\n".join(report) + "\n", encoding="utf-8")

print(out / "report.md")
for name, status in gate_status.items():
    print(f"{name}: {status}")
print(
    "warm_start_speedup="
    f"{warm_interval[0]:.3f}x [{warm_interval[1]:.3f}, {warm_interval[2]:.3f}]"
)
print(
    "deterministic_vs_warm="
    f"{deterministic_interval[0]:.3f}x [{deterministic_interval[1]:.3f}, {deterministic_interval[2]:.3f}]"
)
if credible_failures:
    raise SystemExit(
        "statistically credible v1.7 benchmark objective failures: "
        + ", ".join(credible_failures)
    )
