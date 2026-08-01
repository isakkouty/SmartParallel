# Analysis and documentation tools

## `analyze_v15_adaptive_routes.py`

Validates the v1.5 raw threshold-route matrix before performance analysis. It rejects missing rows, correctness failures, route-authentication failures, inconsistent addresses/alignment, unbalanced route orders, probe leakage, malformed dispatch batches, and inconsistent Native-kernel identity. It recomputes medians, route regret, Native-oracle quality, and confidence-bounded dispatch overhead, then writes summary CSV, Markdown, and the compatibility Auto-regret SVG.

```text
py -3 tools/analyze_v15_adaptive_routes.py RAW.csv OUTPUT_DIR
```

## `generate_v15_benchmark_plots.py`

Generates dependency-free SVG figures from a validated v1.5 publication directory:

- automatic speedup versus direct sequential and direct OpenCV;
- route-selection regret;
- Native kernel versus independent oracle;
- stable Auto dispatch overhead and confidence intervals;
- initial versus settled route map.

```text
py -3 tools/generate_v15_benchmark_plots.py SUMMARY.csv RAW.csv LEARNING.csv OUTPUT_DIR
```

The v1.5 release validation script runs this tool automatically.

## `publish_v15_benchmark_docs.py`

Publishes a validated 6/6 v1.5 run into `docs/v1.5/assets/benchmarks/`. It refuses incomplete or failing runs, copies accepted evidence, generates figures, writes machine-readable metrics and generated Markdown, and records source hashes.

```text
py -3 tools/publish_v15_benchmark_docs.py validation/output/v1.5.0_adaptive_routes/publication_<timestamp>
```

## `plot_real_world_results.py`

Validates the retained v1.1 real-world CSV schemas, correctness flags, backend authentication, and four-participant budget, then generates the v1.1 PNG/SVG figures and aggregate metrics.

```text
python -m pip install pandas matplotlib
python tools/plot_real_world_results.py
```

## `check_documentation.py`

Validates local Markdown links, rejects malformed control characters, and checks version/archive markers.

```text
py -3 tools/check_documentation.py
```

## Historical analysis tools

- `plot_benchmark.py` — generic plot utility used by earlier workflows.
- `phase1_dataset_audit.py` — audits historical Phase 1 calibration/holdout datasets.
- `phase1_regret_ranker.py` — trains and evaluates the historical regret-aware utility ranker.

Python cache directories are local artifacts and must not be committed.

## `generate_source_manifest.py`

Generates a deterministic SHA-256 manifest for the source tree while excluding local build, dependency, install, and validation-output directories.

```text
py -3 tools/generate_source_manifest.py validation/output/<publication>/source-hashes.txt
```

## `prepare_v16_publication_archive.py`

Preserves installed-consumer CTest evidence, removes temporary install/build trees, rejects remaining compiled binaries, and verifies that the environment record does not contain known private-system fields before a v1.6 publication ZIP is created.

```text
py -3 tools/prepare_v16_publication_archive.py validation/output/v1.6.0_scientific_foundations/publication_<timestamp>
```

## `analyze_v16_scientific_foundations.py`

Validates v1.6 schema-v2 scientific evidence. It distinguishes IEEE execution validity from required reference accuracy, checks complete-output digests, verifies reduction and pointwise cross-scheduler matrices, authenticates numerical plans and routes, evaluates adversarial accuracy improvement, applies the balanced Fast compatibility gate, and generates summary CSV, metrics JSON, a Markdown report, and nine SVG figures.

```text
py -3 tools/analyze_v16_scientific_foundations.py RAW.csv OUTPUT_DIR
```

## `create_reproducible_zip.py`

Creates a deterministic ZIP from an already curated directory. It is used for publication-evidence bundles.

```text
py -3 tools/create_reproducible_zip.py SOURCE_DIRECTORY OUTPUT.zip
```

## `create_source_release_zip.py`

Creates the clean SmartParallel source release. It regenerates the source manifest, excludes generated/local trees, rejects LF-only Windows command files, normalizes timestamps and ordering, and preserves executable permissions.

```text
py -3 tools/create_source_release_zip.py SmartParallel-v1.6.0-source.zip --root-name SmartParallel-1.6.0
```
