#!/usr/bin/env python3
"""Generate a deterministic SHA-256 manifest for the SmartParallel source tree."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOP_LEVEL_FILES = {
    ".clang-format",
    ".gitattributes",
    ".gitignore",
    "CHANGELOG.md",
    "CMakeLists.txt",
    "CMakePresets.json",
    "LICENSE",
    "README.md",
    "vcpkg.json",
}
SOURCE_DIRECTORIES = {
    ".github",
    "benchmarks",
    "cmake",
    "docs",
    "examples",
    "include",
    "integrations",
    "scripts",
    "src",
    "tests",
    "tools",
    "validation",
    "vision",
}
EXCLUDED_DIRECTORY_NAMES = {
    ".git",
    ".vs",
    "__pycache__",
    "_deps",
    "build",
    "dist",
    "install",
    "out",
    "vcpkg_installed",
}


def is_included(path: Path, output: Path) -> bool:
    if path == output or not path.is_file():
        return False
    relative = path.relative_to(ROOT)
    if len(relative.parts) == 1:
        return relative.name in TOP_LEVEL_FILES
    if relative.parts[0] not in SOURCE_DIRECTORIES:
        return False
    if relative.parts[:2] == ("validation", "output"):
        return False
    return not any(
        part in EXCLUDED_DIRECTORY_NAMES or part.startswith("cmake-build-")
        for part in relative.parts[:-1]
    )


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path, help="manifest output path")
    args = parser.parse_args()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    files = sorted(
        (path for path in ROOT.rglob("*") if is_included(path, output)),
        key=lambda path: path.relative_to(ROOT).as_posix(),
    )
    with output.open("w", encoding="utf-8", newline="\n") as manifest:
        for path in files:
            manifest.write(f"{digest(path)}  {path.relative_to(ROOT).as_posix()}\n")
    print(f"Source manifest: {output} ({len(files)} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
