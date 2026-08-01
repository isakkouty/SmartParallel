#!/usr/bin/env python3
"""Verify the embedded deterministic SmartParallel source manifest."""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path, PurePosixPath

# Reuse the archive inclusion contract so creation and verification cannot drift.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from create_source_release_zip import included  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "SOURCE_MANIFEST.sha256"


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def fail(message: str) -> None:
    raise SystemExit(f"Source manifest verification failed: {message}")


def main() -> int:
    if not MANIFEST.is_file():
        fail("SOURCE_MANIFEST.sha256 is missing")

    expected: dict[str, str] = {}
    for line_number, raw_line in enumerate(
        MANIFEST.read_text(encoding="utf-8").splitlines(), start=1
    ):
        if not raw_line:
            fail(f"blank line at {line_number}")
        if "  " not in raw_line:
            fail(f"malformed line {line_number}")
        hash_text, relative_text = raw_line.split("  ", 1)
        if len(hash_text) != 64 or any(c not in "0123456789abcdef" for c in hash_text):
            fail(f"invalid SHA-256 at line {line_number}")
        relative = PurePosixPath(relative_text)
        if relative.is_absolute() or ".." in relative.parts or not relative.parts:
            fail(f"unsafe path at line {line_number}: {relative_text}")
        normalized = relative.as_posix()
        if normalized in expected:
            fail(f"duplicate path at line {line_number}: {normalized}")
        expected[normalized] = hash_text

    actual_paths = {
        path.relative_to(ROOT).as_posix(): path
        for path in ROOT.rglob("*")
        if included(path, ROOT / "__manifest_verification__.zip", False)
    }
    missing = sorted(set(expected) - set(actual_paths))
    extra = sorted(set(actual_paths) - set(expected))
    if missing:
        fail("missing files: " + ", ".join(missing[:10]))
    if extra:
        fail("unlisted files: " + ", ".join(extra[:10]))

    changed = [
        relative
        for relative, expected_hash in expected.items()
        if digest(actual_paths[relative]) != expected_hash
    ]
    if changed:
        fail("hash mismatch: " + ", ".join(changed[:10]))

    print(f"Source manifest verification: PASS ({len(expected)} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
