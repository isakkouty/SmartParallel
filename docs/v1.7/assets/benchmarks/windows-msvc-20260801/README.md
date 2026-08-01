# SmartParallel v1.7 Windows/MSVC accepted publication

This directory contains the documentation evidence copied from the successful August 1, 2026 Windows release workflow.

## Environment identity

- Operating system family: Windows
- Compiler: MSVC 19.44 (`compiler_version: 1944`)
- Architecture: x86-64
- Recorded hardware identity: AuthenticAMD family 23 model 8, 16 logical / 8 physical processors
- Build type: Release
- oneTBB: enabled for the primary publication
- OpenCV: validated in a separate focused provider matrix

## v1.7 headline results

- Adaptive warm-start speedup: **2.600×** (95% interval **2.500–2.764×**)
- Deterministic/warm latency ratio: **1.014×** (95% interval **0.959–1.084×**)
- Explicit Runtime paired overhead: **9.900 µs** (interval crosses the 20 µs objective: `INCONCLUSIVE-PASS`)
- Copied-context paired overhead: **-0.100 µs** (interval crosses the objective: `INCONCLUSIVE-PASS`)
- 1,000-entry profile load: **713.304 ms** (95% interval **708.684–716.005 ms**)
- All objective gates accepted

## Validation matrix

- main regression: **24/24 passed**;
- native-only regression: **24/24 passed**;
- oneTBB + OpenCV focused matrix: **3/3 passed**;
- exact returned ZIP matrix: **6/6 passed**;
- installed core, profile, Vision, and OpenCV consumers: passed;
- installed calibration, explicit approval, and two-process replay: passed;
- documentation validation: passed in the source tree and exact ZIP;
- retained v1.6 suite: **3,936 samples**, all gates passed.

## Cross-process identity

The two fresh replay manifests are byte-identical with SHA-256:

```text
caa94172f51f4a161658ed39fff102340186ea6f3bba4f327a5a3fa2694e898c
```

Their output digest is:

```text
b10ced4bee0617873433aff5e2ea369135e9b48163924eb1ff249aec6756d3d3
```

## Files

- `accepted-raw.csv`, `accepted-summary.csv`, `benchmark-metrics.json`, and `accepted-publication-report.md` — v1.7 benchmark evidence;
- `01_*.svg` through `09_*.svg` — generated v1.7 figures;
- `accepted-validation-summary.md` — release matrix summary;
- `cross-process/` — Approved profile, calibration report, two replay manifests, sidecar hashes, and comparison output;
- `v1.6-regression/` — retained scientific report, metrics, and figures;
- `source-zip.sha256` — accepted source-archive identity before this documentation-only update.
