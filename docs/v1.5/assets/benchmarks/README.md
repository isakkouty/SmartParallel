# SmartParallel v1.5 benchmark assets

These files are generated from the accepted v1.5 publication run and support the public benchmark report.

- `generated-results.md` — generated table and aggregate metrics.
- `benchmark-metrics.json` — machine-readable accepted metrics.
- `accepted-summary.csv` — six-preset proof-gate summary.
- `accepted-learning.csv` — route-learning and current-context adaptation evidence.
- `accepted-environment.txt` — compiler, OpenCV, worker-budget, and build configuration.
- `accepted-publication-report.md` — analyzer-generated release-gate report.
- `source-hashes.txt` — hashes of the accepted source evidence.
- `accepted-publication.zip` — full accepted raw evidence, learning data, environment, report, build log, and figures.
- `v1.5.0_*.svg` — dependency-free publication figures.

Regenerate a publication run with:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v15_adaptive_routes_release_validation.bat 31
```

Publish a validated run into this directory with:

```bat
py -3 tools\publish_v15_benchmark_docs.py validation\output\v1.5.0_adaptive_routes\publication_<timestamp>
```

Performance is machine-specific. Do not copy values from one machine into universal performance claims.
