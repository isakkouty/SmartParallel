# SmartParallel v1.6 benchmark reproduction

## Windows schema-v2 publication

From a Visual Studio x64 developer environment:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v16_scientific_foundations_release_validation.bat 31
```

Replace the vcpkg path as needed. The workflow validates the primary dependency-enabled configuration and a separately configured oneTBB/OpenCV-disabled regression. The secondary configuration is launched without inherited vcpkg/toolchain state and authenticates `SMARTPARALLEL_HAS_TBB=0` and `SMARTPARALLEL_VISION_HAS_OPENCV=0` before testing.

## Linux/macOS full workflow

```sh
scripts/validation/run_v16_scientific_foundations_release_validation.sh 21 full
```

For a development smoke run:

```sh
scripts/validation/run_v16_scientific_foundations_release_validation.sh 5 quick
```

## Outputs

Results are written below:

```text
validation/output/v1.6.0_scientific_foundations/publication_<run-stamp>/
```

The workflow produces:

- schema-v2 raw CSV;
- generated summary CSV;
- metrics JSON;
- Markdown validation report;
- nine SVG plots;
- heat-pilot output;
- sanitized environment metadata;
- CTest and installed-consumer logs;
- deterministic source hashes;
- a publication evidence ZIP.

## Publication controls

The workflow:

- builds in Release mode without supported unsafe fast-math flags;
- runs the complete deterministic suite;
- executes full-output validation outside timed regions;
- authenticates requested numerical policy, plan, scheduler, and workers;
- preserves raw samples before analysis;
- runs no-oneTBB, sanitizer, compiler, installation, documentation, and archive checks in full mode where supported;
- removes temporary builds and binary artifacts from evidence archives;
- creates ZIP files with normalized timestamps and deterministic ordering.

Performance runs should be compared only when compiler, binary, flags, CPU, scheduler settings, inputs, and evidence schema are compatible.

## Source archive portability

The clean release source ZIP is created from the repository root with:

```sh
python3 tools/create_source_release_zip.py SmartParallel-v1.6.0-source.zip \
  --root-name SmartParallel-1.6.0
```

The source packager:

- regenerates `SOURCE_MANIFEST.sha256`;
- excludes build, dependency, install, cache, and `validation/output` trees;
- requires CRLF for Windows `.bat`/`.cmd` files;
- normalizes ZIP timestamps and file ordering;
- preserves executable permissions.

Use `create_reproducible_zip.py` for already curated publication-evidence directories.

## Publish reviewed evidence

A completed publication is not copied into the public documentation automatically. After review:

```bat
py -3 tools\publish_v16_benchmark_docs.py validation\output\v1.6.0_scientific_foundations\publication_<timestamp> --platform-label windows-msvc-<date>
```

Use `--promote-primary` only for the run selected as the project’s primary accepted evidence. The publisher rejects failed numerical, reproducibility, route-authentication, Fast-compatibility, or performance-sanity gates.
