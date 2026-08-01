# Windows/MSVC schema-v2 evidence

This directory retains the current Windows benchmark and validation evidence supplied from the July 31, 2026 release workflow.

- MSVC Release build with oneTBB and OpenCV completed.
- Complete CTest regression: **20/20 passed**.
- Scientific benchmark: **3,936 schema-v2 samples**.
- Full-output correctness, reproducibility, route authentication, numerical capability, reference-accuracy, cross-scheduler, and pointwise-plan gates passed.
- Installed core and Vision package consumers passed.
- Documentation validation passed.
- Reanalysis with the corrected paired Fast-compatibility method produced **1.0000×**, with a 90% robust interval of **0.9764–1.0242×**: **Pass**.

The original workflow then stopped before completing the Windows no-oneTBB matrix because the LF-only batch file could not reliably resolve its local label. That script defect is fixed in the source release. The no-oneTBB/no-OpenCV matrix is independently validated in the accepted Linux evidence and must be rerun on Windows before claiming that specific Windows matrix as completed.
