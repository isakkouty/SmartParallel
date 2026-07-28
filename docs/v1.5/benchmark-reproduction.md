# v1.5 benchmark reproduction

## Publication workflow

From Command Prompt, with `VCPKG_ROOT` pointing to a vcpkg checkout:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v15_adaptive_routes_release_validation.bat 31
```

The script can be launched from any current directory. It:

1. configures an MSVC Release build;
2. enables the vision module, OpenCV provider, and required oneTBB;
3. installs vcpkg's `vision-opencv` manifest feature;
4. builds the library, complete deterministic test suite, v1.5 test, example-capable module, and benchmark;
5. runs CTest;
6. records one cold Auto sample per preset;
7. performs balanced initial learning until the selector reports a holdout-verified route;
8. enters the balanced interleaved deployment regime with sparse drift sentinels and current-context ABBA revalidation enabled;
9. requires the adapted route to remain clean and unchanged for a bounded settling streak;
10. pauses route maintenance without changing the profile key, then records the requested odd steady-state matrix using one identical destination allocation for every route in a preset;
11. validates correctness, route authentication, identical addresses/alignment, deployment settling, route-switch telemetry, and probe isolation;
12. runs a dedicated adjacent Auto/selected-forced ABBA/BAAB batch matrix and computes confidence-bounded overhead;
13. evaluates Native Sequential against an independent compiler-generated reference loop and records the selected Native CPU kernel;
14. writes Markdown, CSV, learning telemetry, hashes, and dependency-free SVG evidence;
15. creates a shareable ZIP of the publication output.

Development run:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v15_adaptive_routes_development.bat
```

Reuse an existing valid build:

```bat
scripts\validation\run_v15_adaptive_routes_release_validation.bat 31 reuse
```

## Outputs

Each run creates a timestamped directory and ZIP under:

```text
validation/output/v1.5.0_adaptive_routes/
```

Core evidence:

- `v1.5.0_adaptive_routes_raw.csv`;
- `v1.5.0_adaptive_routes_learning.csv`;
- `v1.5.0_adaptive_routes_environment.txt` with complete OpenCV build information;
- `v1.5.0_build_vectorization.log` with the MSVC build output and `/Qvec-report:2` diagnostic probe evidence;
- `v1.5.0_adaptive_routes.csv`;
- `v1.5.0_adaptive_routes_report.md`.

Publication figures:

- `v1.5.0_automatic_speedup.svg`;
- `v1.5.0_route_selection_regret.svg`;
- `v1.5.0_native_kernel_vs_oracle.svg`;
- `v1.5.0_dispatch_overhead.svg`;
- `v1.5.0_adaptive_route_map.svg`;
- `v1.5.0_auto_regret.svg` retained for compatibility with earlier publication bundles.

## Publishing an accepted run into the documentation

After a run passes all six combined proof gates, copy its validated data and figures into the documentation tree with:

```bat
py -3 tools\publish_v15_benchmark_docs.py validation\output\v1.5.0_adaptive_routes\publication_<timestamp>
```

The tool refuses to publish a run unless all six presets passed and every raw row is correct. It writes:

- generated Markdown results;
- machine-readable JSON metrics;
- accepted summary and learning CSVs;
- environment metadata and source hashes;
- all publication SVG figures.

Run the documentation validator afterward:

```bat
py -3 tools\check_documentation.py
```

## Evidence policy

The publication benchmark uses deterministic generated input, exact byte-level correctness, no timed disk I/O, one shared destination allocation per preset, destination reset outside timed sections, balanced route orders, odd observed-sample medians, and separate cold, learning, deployment-settling, steady-state, and dispatch phases.

Raw schema v6 records exact addresses, alignment, Native-kernel identity, initial-learning and deployment call counts, route switches, pair order/position, and all probe states. Learning schema v2 records training and current baselines plus the latest current-context revalidation comparison.

Production maintenance is paused only after deployment adaptation has settled, so the steady-state matrix contains clean learned hot-route executions. Drift detection and revalidation remain enabled in production and are validated deterministically.

## Release gates

- **Route selection:** the forced implementation matching the settled route must be within 5% or 1 µs of the fastest eligible forced route.
- **Stable dispatch:** the batched adjacent ABBA/BAAB Auto overhead must not have a lower robust 95% bound above 1 µs.
- **Native kernel:** Native Sequential must be within 10% or 0.5 µs of the independent compiler loop.

OpenCV-versus-Native comparisons remain machine-specific. The framework does not require the Native kernel, OpenCV, or any scheduler to win universally.
