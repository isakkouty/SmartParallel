# SmartParallel v1.6 accepted benchmark evidence

The files at this directory level form the corrected **schema-v2 Linux/GCC/x86-64 accepted publication**.

## Accepted corrected evidence

- **2,442 raw rows**;
- full-output AXPY, stencil, and heat-diffusion validation;
- separate execution-validity and reference-accuracy fields;
- complete-output digests;
- sum, AXPY, and stencil cross-scheduler matrices;
- deterministic reduction and pointwise plan authentication;
- Accurate adversarial sum and dot error **3000 → 0**;
- policy-aware Fast / retained Fast paired median **1.0634×**, with a 90% robust interval of **0.9739–1.1611×** (**inconclusive-pass**);
- largest Fast AXPY, dot, norm, stencil, and heat speedups **1.19×**, **2.35×**, **2.96×**, **3.78×**, and **2.14×** over compact direct-sequential references;
- nine generated SVG plots.

Files:

- `accepted-raw.csv` — complete raw schema-v2 evidence;
- `accepted-summary.csv` — generated robust statistics;
- `benchmark-metrics.json` — release gates and extracted headline values;
- `accepted-publication-report.md` — generated human-readable report;
- `accepted-validation-summary.md` — release validation summary;
- `accepted-environment.txt` — sanitized publication environment;
- `accepted-heat-diffusion-pilot.txt` — pilot evidence summary;
- `source-hashes.txt` — deterministic project source manifest from the accepted workflow;
- `evidence-hashes.txt` — SHA-256 identities for the accepted raw, summary, metrics, report, and environment evidence;
- `accepted-publication.zip` — compact accepted evidence archive;
- `v1.6.0_*.svg` — publication plots.

## Windows evidence

`historical-windows-msvc-20260731-pre-correction/` preserves the earlier Windows schema-v1 run only for development traceability. It predates:

- fixed deterministic pointwise plans;
- complete-output benchmark validation;
- separate execution validity/reference accuracy;
- balanced adjacent Fast compatibility measurement.

`historical-windows-msvc-20260731-pre-validated-pointer-kernels/` preserves a later schema-v2 run. It completed 3,936 samples, every numerical gate, both 20/20 test matrices, documentation validation, and installed consumers. It predates the final validated pointer/stride kernels and scientific-kernel performance-sanity gate, so it is retained for workflow and numerical traceability only. Rerun the final source before publishing current Windows performance evidence:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v16_scientific_foundations_release_validation.bat 31
```

## Regeneration

Linux/macOS:

```sh
scripts/validation/run_v16_scientific_foundations_release_validation.sh 21 full
```

Every measurement remains machine-specific.

## Publishing a validated run

After a release-validation run completes, copy its checked evidence into the documentation with:

```bat
py -3 tools\publish_v16_benchmark_docs.py validation\output\v1.6.0_scientific_foundations\publication_<timestamp> --platform-label windows-msvc-<date>
```

Add `--promote-primary` only after reviewing the report and confirming every correctness, reproducibility, authentication, Fast-compatibility, and scientific-kernel performance-sanity gate.
