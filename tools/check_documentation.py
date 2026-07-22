#!/usr/bin/env python3
"""Validate local Markdown links and reject malformed control characters."""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
FENCE = re.compile(r"```.*?```", re.DOTALL)
INLINE_CODE = re.compile(r"`[^`]*`")


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

    if errors:
        print("Documentation validation: FAIL")
        for error in errors:
            print(f"- {error}")
        return 1
    print("Documentation validation: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
