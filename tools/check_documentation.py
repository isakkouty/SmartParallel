#!/usr/bin/env python3
"""Validate local Markdown links and reject malformed control characters."""

from __future__ import annotations

import json
import re
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
FENCE = re.compile(r"```.*?```", re.DOTALL)
INLINE_CODE = re.compile(r"`[^`]*`")

EXCLUDED_DIRECTORY_NAMES = {
    ".git",
    ".vs",
    "__pycache__",
    "_deps",
    "build",
    "dist",
    "out",
    "vcpkg_installed",
}


def is_excluded_path(path: Path) -> bool:
    relative = path.relative_to(ROOT)
    directory_parts = relative.parts[:-1]

    if any(
        part in EXCLUDED_DIRECTORY_NAMES
        or part.startswith("cmake-build-")
        for part in directory_parts
    ):
        return True

    # Generated SmartParallel publication reports are outputs, not source docs.
    if len(relative.parts) >= 2 and relative.parts[:2] == ("validation", "output"):
        return True

    return False


def iter_project_markdown():
    for path in ROOT.rglob("*.md"):
        if not is_excluded_path(path):
            yield path




def validate_posix_scripts(errors: list[str]) -> None:
    scripts = sorted(list(ROOT.rglob("*.sh")) + list(ROOT.rglob("*.py")))
    for path in scripts:
        if is_excluded_path(path):
            continue
        raw = path.read_bytes()
        crlf = raw.count(b"\r\n")
        if crlf:
            errors.append(
                f"{path.relative_to(ROOT)}: POSIX/Python script contains {crlf} CRLF line endings"
            )


def validate_windows_scripts(errors: list[str]) -> None:
    scripts = sorted(list(ROOT.rglob("*.bat")) + list(ROOT.rglob("*.cmd")))
    for path in scripts:
        if is_excluded_path(path):
            continue
        raw = path.read_bytes()
        bare_lf = raw.replace(b"\r\n", b"").count(b"\n")
        if bare_lf:
            errors.append(
                f"{path.relative_to(ROOT)}: Windows command file contains {bare_lf} bare LF line endings"
            )

    release_script = (
        ROOT / "scripts/validation/run_v16_scientific_foundations_release_validation.bat"
    )
    if release_script.exists():
        text = release_script.read_text(encoding="ascii")
        required_fragments = [
            "Validate isolated no-oneTBB/no-OpenCV build",
            "--unset=VCPKG_ROOT",
            "SMARTPARALLEL_HAS_TBB=0",
            "SMARTPARALLEL_VISION_HAS_OPENCV=0",
        ]
        for fragment in required_fragments:
            if fragment not in text:
                errors.append(
                    f"{release_script.relative_to(ROOT)}: missing isolated-matrix guard {fragment!r}"
                )
        if "call :configure_no_tbb" in text:
            errors.append(
                f"{release_script.relative_to(ROOT)}: fragile batch-label no-oneTBB dispatch remains"
            )

def validate_v16_assets(errors: list[str]) -> None:
    assets = ROOT / "docs/v1.6/assets/benchmarks"
    required = [
        "README.md",
        "accepted-raw.csv",
        "accepted-summary.csv",
        "benchmark-metrics.json",
        "accepted-publication-report.md",
        "accepted-environment.txt",
        "accepted-heat-diffusion-pilot.txt",
        "accepted-validation-summary.md",
        "accepted-publication.zip",
        "source-hashes.txt",
        "evidence-hashes.txt",
        "v1.6.0_policy_execution_time.svg",
        "v1.6.0_numerical_error.svg",
        "v1.6.0_canonical_scaling.svg",
        "v1.6.0_axpy_throughput.svg",
        "v1.6.0_dot_norm_throughput.svg",
        "v1.6.0_stencil_throughput.svg",
        "v1.6.0_heat_diffusion_speed.svg",
        "v1.6.0_reproducibility_matrix.svg",
        "v1.6.0_fast_mode_regression.svg",
    ]
    for name in required:
        path = assets / name
        if not path.exists() or path.stat().st_size == 0:
            errors.append(f"docs/v1.6/assets/benchmarks: missing or empty {name}")

    metrics_path = assets / "benchmark-metrics.json"
    if metrics_path.exists():
        try:
            metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"{metrics_path.relative_to(ROOT)}: invalid JSON: {exc}")
        else:
            if metrics.get("schema_version") != 2:
                errors.append(f"{metrics_path.relative_to(ROOT)}: unexpected schema version")
            if not all(metrics.get("gates", {}).values()):
                errors.append(f"{metrics_path.relative_to(ROOT)}: one or more evidence gates failed")
            if not all(metrics.get("accuracy_gates", {}).values()):
                errors.append(f"{metrics_path.relative_to(ROOT)}: one or more accuracy gates failed")
            if not all(metrics.get("reproducibility_matrix_gates", {}).values()):
                errors.append(f"{metrics_path.relative_to(ROOT)}: reproducibility matrix failed")
            if not metrics.get("pointwise_plan_gate"):
                errors.append(f"{metrics_path.relative_to(ROOT)}: pointwise plan gate failed")
            if not metrics.get("scientific_kernel_performance_sanity_gate"):
                errors.append(
                    f"{metrics_path.relative_to(ROOT)}: scientific-kernel performance sanity failed"
                )
            minimum = metrics.get("scientific_kernel_performance_sanity_minimum_speedup")
            speedups = metrics.get("scientific_kernel_speedup_vs_direct_sequential", {})
            if not isinstance(minimum, (int, float)) or minimum <= 0:
                errors.append(f"{metrics_path.relative_to(ROOT)}: malformed performance threshold")
            elif (not isinstance(speedups, dict) or set(speedups) != {
                    "axpy", "dot", "norm", "stencil_2d", "heat_diffusion_20"
                } or any(not isinstance(value, (int, float)) or value < minimum
                         for value in speedups.values())):
                errors.append(f"{metrics_path.relative_to(ROOT)}: malformed/failing speedup evidence")
            if not metrics.get("fast_mode_regression_gate"):
                errors.append(f"{metrics_path.relative_to(ROOT)}: Fast compatibility gate failed")
            if metrics.get("fast_mode_regression_status") not in {"pass", "inconclusive-pass"}:
                errors.append(f"{metrics_path.relative_to(ROOT)}: invalid Fast compatibility status")
            interval = metrics.get("fast_mode_paired_ratio_interval_90", [])
            if (not isinstance(interval, list) or len(interval) != 2
                    or not all(isinstance(value, (int, float)) for value in interval)):
                errors.append(f"{metrics_path.relative_to(ROOT)}: malformed Fast compatibility interval")

    raw_path = assets / "accepted-raw.csv"
    if raw_path.exists():
        try:
            header = raw_path.open(encoding="utf-8").readline().strip().split(",")
            first = raw_path.open(encoding="utf-8").readlines()[1].split(",")
        except (OSError, IndexError) as exc:
            errors.append(f"{raw_path.relative_to(ROOT)}: unreadable evidence: {exc}")
        else:
            if "execution_valid" not in header or "reference_accuracy_pass" not in header:
                errors.append(f"{raw_path.relative_to(ROOT)}: missing schema-v2 validation fields")
            if not first or first[0] != "2":
                errors.append(f"{raw_path.relative_to(ROOT)}: expected schema version 2")

    historical_directories = [
        assets / "historical-windows-msvc-20260731-pre-correction",
        assets / "historical-windows-msvc-20260731-pre-validated-pointer-kernels",
    ]
    for historical in historical_directories:
        historical_readme = historical / "README.md"
        if historical.exists():
            if not historical_readme.exists():
                errors.append(f"{historical.relative_to(ROOT)}: missing historical-evidence README")
            elif "historical" not in historical_readme.read_text(encoding="utf-8").lower():
                errors.append(f"{historical_readme.relative_to(ROOT)}: not clearly marked historical")

def validate_v15_assets(errors: list[str]) -> None:
    assets = ROOT / "docs/v1.5/assets/benchmarks"
    required = [
        "README.md",
        "generated-results.md",
        "benchmark-metrics.json",
        "accepted-summary.csv",
        "accepted-learning.csv",
        "accepted-environment.txt",
        "accepted-publication-report.md",
        "accepted-publication.zip",
        "source-hashes.txt",
        "v1.5.0_automatic_speedup.svg",
        "v1.5.0_route_selection_regret.svg",
        "v1.5.0_native_kernel_vs_oracle.svg",
        "v1.5.0_dispatch_overhead.svg",
        "v1.5.0_adaptive_route_map.svg",
    ]
    for name in required:
        path = assets / name
        if not path.exists() or path.stat().st_size == 0:
            errors.append(f"docs/v1.5/assets/benchmarks: missing or empty {name}")

    metrics_path = assets / "benchmark-metrics.json"
    if metrics_path.exists():
        try:
            metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"{metrics_path.relative_to(ROOT)}: invalid JSON: {exc}")
        else:
            expected = {
                "release": "1.5.0",
                "raw_sample_count": 2238,
                "correctness_failures": 0,
                "authentication_failures": 0,
                "combined_gate_passes": 6,
                "combined_gate_total": 6,
            }
            for key, value in expected.items():
                if metrics.get(key) != value:
                    errors.append(
                        f"{metrics_path.relative_to(ROOT)}: expected {key}={value!r}, "
                        f"found {metrics.get(key)!r}"
                    )
            if len(metrics.get("presets", [])) != 6:
                errors.append(f"{metrics_path.relative_to(ROOT)}: expected six presets")
            hashes = metrics.get("hashes", {})
            if any(not re.fullmatch(r"[0-9a-f]{64}", str(value)) for value in hashes.values()):
                errors.append(f"{metrics_path.relative_to(ROOT)}: malformed evidence hash")

    archive_path = assets / "accepted-publication.zip"
    if archive_path.exists():
        try:
            with zipfile.ZipFile(archive_path) as archive:
                names = set(archive.namelist())
        except zipfile.BadZipFile:
            errors.append(f"{archive_path.relative_to(ROOT)}: invalid ZIP")
        else:
            required_members = {
                "v1.5.0_adaptive_routes_raw.csv",
                "v1.5.0_adaptive_routes_learning.csv",
                "v1.5.0_adaptive_routes.csv",
                "v1.5.0_adaptive_routes_environment.txt",
                "v1.5.0_adaptive_routes_report.md",
                "v1.5.0_build_vectorization.log",
            }
            missing = required_members - names
            if missing:
                errors.append(
                    f"{archive_path.relative_to(ROOT)}: missing evidence {sorted(missing)}"
                )


def validate_release_metadata(errors: list[str]) -> None:
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    version_match = re.search(r"project\(\s*SmartParallel\s+VERSION\s+([0-9.]+)", cmake_text)
    cmake_version = version_match.group(1) if version_match else None
    try:
        package = json.loads((ROOT / "vcpkg.json").read_text(encoding="utf-8"))
        vcpkg_version = package.get("version-string")
    except json.JSONDecodeError as exc:
        errors.append(f"vcpkg.json: invalid JSON: {exc}")
        vcpkg_version = None
    if cmake_version != "1.6.0":
        errors.append(f"CMakeLists.txt: expected project version 1.6.0, found {cmake_version!r}")
    if vcpkg_version != cmake_version:
        errors.append(
            f"release version mismatch: CMake={cmake_version!r}, vcpkg={vcpkg_version!r}"
        )


def main() -> int:
    errors: list[str] = []
    for path in iter_project_markdown():
        raw = path.read_bytes()
        for offset, byte in enumerate(raw):
            if byte < 32 and byte not in (9, 10, 13):
                errors.append(f"{path.relative_to(ROOT)}: control byte {byte} at offset {offset}")
                break

        text = raw.decode("utf-8", errors="strict")
        searchable = INLINE_CODE.sub("", FENCE.sub("", text))
        for match in LINK.finditer(searchable):
            target = match.group(1).strip()
            if target.startswith("<") and target.endswith(">"):
                target = target[1:-1]
            target = target.split("#", 1)[0]
            if not target or "://" in target or target.startswith(("mailto:", "data:")):
                continue
            if ' "' in target:
                target = target.split(' "', 1)[0]
            destination = (path.parent / target).resolve()
            if not destination.exists():
                errors.append(f"{path.relative_to(ROOT)}: broken link {match.group(1)!r}")

    for path in (ROOT / "docs/v1.6").glob("*.md"):
        text = path.read_text(encoding="utf-8")
        if "v1.6" not in text[:500]:
            errors.append(f"{path.relative_to(ROOT)}: missing v1.6 release marker")

    for path in (ROOT / "docs/v1.5").glob("*.md"):
        text = path.read_text(encoding="utf-8")
        if "v1.5" not in text[:500]:
            errors.append(f"{path.relative_to(ROOT)}: missing v1.5 release marker")

    for path in (ROOT / "docs/v1.4").glob("*.md"):
        text = path.read_text(encoding="utf-8")
        if "v1.4" not in text[:500]:
            errors.append(f"{path.relative_to(ROOT)}: missing v1.4 release marker")

    for path in (ROOT / "docs/v1.3").glob("*.md"):
        text = path.read_text(encoding="utf-8")
        if "v1.3" not in text[:500]:
            errors.append(f"{path.relative_to(ROOT)}: missing v1.3 release marker")

    for path in (ROOT / "docs/v1.1").glob("*.md"):
        text = path.read_text(encoding="utf-8")
        if path.name == "README.md":
            required = "Runtime feature baseline"
        else:
            required = "Runtime documentation"
        if required not in text[:400]:
            errors.append(f"{path.relative_to(ROOT)}: missing retained v1.1 runtime marker")

    for path in (ROOT / "docs/v1.0").glob("*.md"):
        if "Archived" not in path.read_text(encoding="utf-8")[:300]:
            errors.append(f"{path.relative_to(ROOT)}: missing v1.0 archive marker")

    for path in [ROOT / "docs/v1.3/README.md", ROOT / "docs/v1.4/README.md"]:
        if "Current release" in path.read_text(encoding="utf-8")[:500]:
            errors.append(f"{path.relative_to(ROOT)}: stale current-release marker")

    validate_posix_scripts(errors)
    validate_windows_scripts(errors)
    validate_v16_assets(errors)
    validate_v15_assets(errors)
    validate_release_metadata(errors)

    if errors:
        print("Documentation validation: FAIL")
        for error in errors:
            print(f"- {error}")
        return 1
    print("Documentation validation: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
