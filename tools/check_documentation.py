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
    if cmake_version != "1.5.0":
        errors.append(f"CMakeLists.txt: expected project version 1.5.0, found {cmake_version!r}")
    if vcpkg_version != cmake_version:
        errors.append(
            f"release version mismatch: CMake={cmake_version!r}, vcpkg={vcpkg_version!r}"
        )


def main() -> int:
    errors: list[str] = []
    for path in ROOT.rglob("*.md"):
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
