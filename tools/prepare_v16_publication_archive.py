#!/usr/bin/env python3
"""Preserve validation evidence and remove generated/private publication content."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

CONSUMER_LOGS = {
    "package-consumer-build": "core-package-consumer-test.log",
    "package-consumer-vision-build": "vision-package-consumer-test.log",
}
REMOVE_DIRECTORIES = ("install", *CONSUMER_LOGS.keys())
FORBIDDEN_SUFFIXES = {".dll", ".exe", ".exp", ".ilk", ".lib", ".obj", ".pdb"}
PRIVATE_MARKERS = (
    "Registered Owner",
    "Registered Organization",
    "Product ID",
    "Host Name:",
    "Nom de l'hôte",
    "Propriétaire enregistré",
    "Identificateur de produit",
    "Adresse(s) IP",
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("publication_dir", type=Path)
    args = parser.parse_args()
    publication = args.publication_dir.resolve()
    if not publication.is_dir():
        raise SystemExit(f"publication directory does not exist: {publication}")

    for build_name, output_name in CONSUMER_LOGS.items():
        source = publication / build_name / "Testing" / "Temporary" / "LastTest.log"
        target = publication / output_name
        if target.exists():
            continue
        if source.exists():
            shutil.copy2(source, target)
        else:
            raise SystemExit(f"missing consumer test evidence: {source}")

    for name in REMOVE_DIRECTORIES:
        shutil.rmtree(publication / name, ignore_errors=True)

    environment = publication / "v1.6.0_environment.txt"
    if not environment.exists():
        raise SystemExit(f"missing sanitized environment file: {environment}")
    environment_text = environment.read_text(encoding="utf-8", errors="replace")
    for marker in PRIVATE_MARKERS:
        if marker.lower() in environment_text.lower():
            raise SystemExit(f"environment file contains private marker: {marker}")

    binaries = sorted(
        path.relative_to(publication).as_posix()
        for path in publication.rglob("*")
        if path.is_file() and path.suffix.lower() in FORBIDDEN_SUFFIXES
    )
    if binaries:
        raise SystemExit("publication archive still contains binaries:\n" + "\n".join(binaries))

    print(f"Publication cleanup: PASS ({publication})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
