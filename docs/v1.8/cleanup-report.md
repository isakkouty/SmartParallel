# SmartParallel v1.8 — Cleanup report

The canonical v1.8 source package is source-only and deterministic.

## Excluded from the release archive

- CMake build and installation trees;
- `validation/output/` publication working directories;
- executables, libraries, object files, PDBs, and DLLs;
- vcpkg and dependency caches;
- Python `__pycache__`, `.pyc`, and `.pyo` files;
- editor and operating-system metadata;
- nested ZIP archives;
- Rodinia and HotSpot integration code or evidence.

## Retained intentionally

- source code and public headers;
- tests, benchmarks, validation scripts, and packaging tools;
- v1.0–v1.8 documentation;
- final accepted documentation evidence under `docs/*/assets`;
- `SOURCE_MANIFEST.sha256`.

## Enforcement

`.gitignore`, `tools/create_source_release_zip.py`, `tools/verify_source_manifest.py`, and `tools/check_documentation.py` enforce the cleanup contract. The final workflow creates two independent deterministic ZIPs and requires byte-for-byte equality before exact-archive validation.
