#!/usr/bin/env python3
"""Create a clean, deterministic SmartParallel source release archive."""

from __future__ import annotations

import argparse
import hashlib
import os
import stat
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXED_TIME = (2020, 1, 1, 0, 0, 0)
TOP_LEVEL_FILES = {
    ".clang-format",
    ".gitattributes",
    ".gitignore",
    "CHANGELOG.md",
    "CMakeLists.txt",
    "CMakePresets.json",
    "LICENSE",
    "README.md",
    "SOURCE_MANIFEST.sha256",
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


def included(path: Path, output: Path, include_manifest: bool = True) -> bool:
    if path == output or not path.is_file():
        return False
    if path.suffix.lower() == ".zip":
        return False
    relative = path.relative_to(ROOT)
    if len(relative.parts) == 1:
        if not include_manifest and relative.name == "SOURCE_MANIFEST.sha256":
            return False
        return relative.name in TOP_LEVEL_FILES
    if relative.parts[0] not in SOURCE_DIRECTORIES:
        return False
    if relative.parts[:2] == ("validation", "output"):
        return False
    return not any(
        part in EXCLUDED_DIRECTORY_NAMES or part.startswith("cmake-build-")
        for part in relative.parts[:-1]
    )


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_manifest(output_archive: Path) -> None:
    files = sorted(
        (path for path in ROOT.rglob("*") if included(path, output_archive, False)),
        key=lambda path: path.relative_to(ROOT).as_posix(),
    )
    lines = [
        f"{sha256_bytes(path.read_bytes())}  {path.relative_to(ROOT).as_posix()}\n"
        for path in files
    ]
    (ROOT / "SOURCE_MANIFEST.sha256").write_text(
        "".join(lines), encoding="utf-8", newline="\n"
    )


def verify_windows_line_endings(files: list[Path]) -> None:
    errors: list[str] = []
    for path in files:
        if path.suffix.lower() not in {".bat", ".cmd"}:
            continue
        raw = path.read_bytes()
        bare_lf = raw.replace(b"\r\n", b"").count(b"\n")
        if bare_lf:
            errors.append(f"{path.relative_to(ROOT)} ({bare_lf} bare LF)")
    if errors:
        raise SystemExit(
            "Windows command files must use CRLF before packaging:\n- "
            + "\n- ".join(errors)
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--root-name", default="SmartParallel-1.8.0")
    args = parser.parse_args()

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    write_manifest(output)

    files = sorted(
        (path for path in ROOT.rglob("*") if included(path, output)),
        key=lambda path: path.relative_to(ROOT).as_posix(),
    )
    verify_windows_line_endings(files)

    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.unlink(missing_ok=True)
    try:
        with zipfile.ZipFile(
            temporary,
            "w",
            compression=zipfile.ZIP_DEFLATED,
            compresslevel=9,
            strict_timestamps=True,
        ) as archive:
            for path in files:
                relative = path.relative_to(ROOT).as_posix()
                archive_name = f"{args.root_name}/{relative}"
                info = zipfile.ZipInfo(archive_name, FIXED_TIME)
                info.create_system = 3
                # Windows-created source archives do not retain Unix mode bits.
                # Derive executable permissions from the file contract so the
                # returned ZIP behaves consistently on every build host.
                is_executable_script = path.read_bytes().startswith(b"#!")
                permissions = 0o755 if is_executable_script else 0o644
                info.external_attr = (stat.S_IFREG | permissions) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(
                    info,
                    path.read_bytes(),
                    compress_type=zipfile.ZIP_DEFLATED,
                    compresslevel=9,
                )

        expected_names = [
            f"{args.root_name}/{path.relative_to(ROOT).as_posix()}" for path in files
        ]
        with zipfile.ZipFile(temporary, "r") as archive:
            names = archive.namelist()
            if len(names) != len(set(names)):
                raise SystemExit("Source release ZIP contains duplicate entries")
            if names != expected_names:
                raise SystemExit("Source release ZIP entry ordering/content is inconsistent")
            bad_entry = archive.testzip()
            if bad_entry is not None:
                raise SystemExit(f"Source release ZIP integrity failure: {bad_entry}")
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)

    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    print(f"Source release ZIP: {output}")
    print(f"Files: {len(files)}")
    print(f"SHA-256: {digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
