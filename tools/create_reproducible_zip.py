#!/usr/bin/env python3
"""Create a deterministic ZIP with normalized timestamps and stable ordering."""
from __future__ import annotations

import argparse
import os
import stat
import zipfile
from pathlib import Path

FIXED_TIME = (2020, 1, 1, 0, 0, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--root-name", default=None)
    args = parser.parse_args()
    source = args.source.resolve()
    output = args.output.resolve()
    if not source.is_dir():
        raise SystemExit(f"source directory does not exist: {source}")
    output.parent.mkdir(parents=True, exist_ok=True)
    root_name = args.root_name or source.name
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.unlink(missing_ok=True)

    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED,
                         compresslevel=9, strict_timestamps=True) as archive:
        for path in sorted(source.rglob("*"), key=lambda item: item.relative_to(source).as_posix()):
            relative = path.relative_to(source).as_posix()
            archive_name = f"{root_name}/{relative}"
            if path.is_dir():
                info = zipfile.ZipInfo(archive_name.rstrip("/") + "/", FIXED_TIME)
                info.create_system = 3
                info.external_attr = (stat.S_IFDIR | 0o755) << 16
                archive.writestr(info, b"")
                continue
            if not path.is_file():
                continue
            info = zipfile.ZipInfo(archive_name, FIXED_TIME)
            info.create_system = 3
            mode = path.stat().st_mode
            permissions = 0o755 if mode & stat.S_IXUSR else 0o644
            info.external_attr = (stat.S_IFREG | permissions) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            with path.open("rb") as handle:
                archive.writestr(info, handle.read(), compress_type=zipfile.ZIP_DEFLATED,
                                 compresslevel=9)
    os.replace(temporary, output)
    print(f"Reproducible ZIP: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
