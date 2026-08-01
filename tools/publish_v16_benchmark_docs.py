#!/usr/bin/env python3
"""Publish one validated SmartParallel v1.6 publication into the docs tree."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DOCS = ROOT / "docs/v1.6/assets/benchmarks"
REQUIRED = {
    "raw": "v1.6.0_scientific_raw.csv",
    "summary": "v1.6.0_scientific_foundations.csv",
    "metrics": "v1.6.0_scientific_metrics.json",
    "report": "v1.6.0_scientific_foundations_report.md",
    "environment": "v1.6.0_environment.txt",
    "pilot": "v1.6.0_heat_diffusion_pilot.txt",
}
OPTIONAL_LOGS = (
    "ctest-main.log",
    "ctest-no-tbb.log",
    "ctest-sanitizers.log",
    "ctest-clang.log",
    "core-package-consumer-test.log",
    "vision-package-consumer-test.log",
    "documentation-validation.log",
)
PLOTS = (
    "v1.6.0_policy_execution_time.svg",
    "v1.6.0_numerical_error.svg",
    "v1.6.0_canonical_scaling.svg",
    "v1.6.0_axpy_throughput.svg",
    "v1.6.0_dot_norm_throughput.svg",
    "v1.6.0_stencil_throughput.svg",
    "v1.6.0_heat_diffusion_speed.svg",
    "v1.6.0_reproducibility_matrix.svg",
    "v1.6.0_fast_mode_regression.svg",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_metrics(metrics: dict[str, object]) -> None:
    if metrics.get("schema_version") != 2:
        raise ValueError("v1.6 publication must use evidence schema 2")
    sections = (
        "gates",
        "accuracy_gates",
        "reproducibility_matrix_gates",
    )
    for section in sections:
        values = metrics.get(section)
        if not isinstance(values, dict) or not values or not all(values.values()):
            raise ValueError(f"publication has a failed or missing {section} gate")
    for name in (
        "pointwise_plan_gate",
        "scientific_kernel_performance_sanity_gate",
        "fast_mode_regression_gate",
    ):
        if metrics.get(name) is not True:
            raise ValueError(f"publication gate failed: {name}")


def validation_summary(metrics: dict[str, object], environment: str) -> str:
    env = metrics["environment"]
    speedups = metrics["scientific_kernel_speedup_vs_direct_sequential"]
    interval = metrics["fast_mode_paired_ratio_interval_90"]
    errors = metrics["adversarial_errors"]
    return "\n".join([
        "# SmartParallel v1.6 validation summary",
        "",
        f"- Platform: **{env['operating_system']} / {env['architecture']}**",
        f"- Compiler: **{env['compiler']}**",
        f"- CPU: **{env['cpu_description']}**",
        f"- Raw benchmark samples: **{metrics['raw_samples']:,}**",
        "- Evidence schema: **2**",
        "- Execution validity: **Pass**",
        "- Required reference accuracy: **Pass**",
        "- Reproducibility: **Pass**",
        "- Route authentication: **Pass**",
        "- Numerical capability: **Pass**",
        "- Cross-scheduler matrices: **Pass**",
        "- Pointwise plan authentication: **Pass**",
        "- Scientific-kernel performance sanity: **Pass**",
        f"- Accurate adversarial sum error: **{errors['sum_adversarial']['Fast']:.0f} → {errors['sum_adversarial']['Accurate']:.0f}**",
        f"- Accurate adversarial dot error: **{errors['dot_adversarial']['Fast']:.0f} → {errors['dot_adversarial']['Accurate']:.0f}**",
        f"- Fast compatibility paired median: **{metrics['fast_mode_largest_workload_ratio']:.4f}×**",
        f"- Fast compatibility 90% interval: **{interval[0]:.4f}–{interval[1]:.4f}×**",
        f"- Fast compatibility status: **{metrics['fast_mode_regression_status']}**",
        "",
        "## Largest Fast workloads versus direct sequential",
        "",
        "| Operation | Speedup |",
        "|---|---:|",
        *[f"| {name} | {value:.3f}× |" for name, value in speedups.items()],
        "",
        "All performance values are machine-specific. The performance-sanity gate is a broad regression detector, not a universal speed claim.",
        "",
    ])


def copy_publication(publication: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    mapping = {
        "raw.csv": publication / REQUIRED["raw"],
        "summary.csv": publication / REQUIRED["summary"],
        "metrics.json": publication / REQUIRED["metrics"],
        "report.md": publication / REQUIRED["report"],
        "environment.txt": publication / REQUIRED["environment"],
        "heat-diffusion-pilot.txt": publication / REQUIRED["pilot"],
    }
    for name, source in mapping.items():
        shutil.copy2(source, destination / name)
    for name in PLOTS:
        shutil.copy2(publication / name, destination / name)
    for name in OPTIONAL_LOGS:
        source = publication / name
        if source.exists():
            shutil.copy2(source, destination / name)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("publication_dir", type=Path)
    parser.add_argument("--docs-dir", type=Path, default=DEFAULT_DOCS)
    parser.add_argument("--platform-label", help="also retain the run in a named subdirectory")
    parser.add_argument("--promote-primary", action="store_true")
    args = parser.parse_args()

    publication = args.publication_dir.resolve()
    docs = args.docs_dir.resolve()
    missing = [name for name in (*REQUIRED.values(), *PLOTS)
               if not (publication / name).is_file()]
    if missing:
        raise FileNotFoundError(f"publication is incomplete: {missing}")

    metrics = json.loads((publication / REQUIRED["metrics"]).read_text(encoding="utf-8"))
    validate_metrics(metrics)
    environment = (publication / REQUIRED["environment"]).read_text(
        encoding="utf-8", errors="replace")

    if args.platform_label:
        platform_dir = docs / args.platform_label
        if platform_dir.exists():
            shutil.rmtree(platform_dir)
        copy_publication(publication, platform_dir)
        (platform_dir / "validation-summary.md").write_text(
            validation_summary(metrics, environment), encoding="utf-8")

    if args.promote_primary:
        docs.mkdir(parents=True, exist_ok=True)
        primary = {
            "accepted-raw.csv": REQUIRED["raw"],
            "accepted-summary.csv": REQUIRED["summary"],
            "benchmark-metrics.json": REQUIRED["metrics"],
            "accepted-publication-report.md": REQUIRED["report"],
            "accepted-environment.txt": REQUIRED["environment"],
            "accepted-heat-diffusion-pilot.txt": REQUIRED["pilot"],
        }
        for target, source in primary.items():
            shutil.copy2(publication / source, docs / target)
        for name in PLOTS:
            shutil.copy2(publication / name, docs / name)
        (docs / "accepted-validation-summary.md").write_text(
            validation_summary(metrics, environment), encoding="utf-8")

        archive_members = [publication / name for name in REQUIRED.values()]
        archive_members += [publication / name for name in PLOTS]
        archive_members += [publication / name for name in OPTIONAL_LOGS
                            if (publication / name).exists()]
        with zipfile.ZipFile(
            docs / "accepted-publication.zip", "w", zipfile.ZIP_DEFLATED
        ) as archive:
            for member in archive_members:
                archive.write(member, member.name)

        source_manifest = publication / "source-hashes.txt"
        if source_manifest.exists():
            shutil.copy2(source_manifest, docs / "source-hashes.txt")

        hashes = {
            "raw_csv_sha256": sha256(publication / REQUIRED["raw"]),
            "summary_csv_sha256": sha256(publication / REQUIRED["summary"]),
            "metrics_json_sha256": sha256(publication / REQUIRED["metrics"]),
            "report_sha256": sha256(publication / REQUIRED["report"]),
            "environment_sha256": sha256(publication / REQUIRED["environment"]),
        }
        (docs / "evidence-hashes.txt").write_text(
            "".join(f"{digest}  {name}\n" for name, digest in hashes.items()),
            encoding="utf-8",
        )

    if not args.platform_label and not args.promote_primary:
        raise ValueError("select --platform-label, --promote-primary, or both")

    print(f"Published validated v1.6 evidence from {publication}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
