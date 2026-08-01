# SmartParallel v1.7 cross-process heat-diffusion proof

The release pilot proves that an approved experiment can be loaded and replayed by two fresh processes without adaptive maintenance or evidence mutation.

## Workflow

1. `smartparallel_calibrate` generates deterministic input and runs Adaptive calibration.
2. Complete output correctness is checked.
3. Candidate stencil 2D and norm profiles are written.
4. `smartparallel_profile approve` creates a distinct Approved database.
5. Two fresh `smartparallel_replay run` processes load that database ReadOnly in Deterministic mode.
6. Each process writes a stable run manifest and SHA-256 file.
7. `smartparallel_replay compare` requires byte-identical manifests.
8. The workflow verifies that the Approved profile bytes did not change.

## Accepted Windows/MSVC evidence

The final release workflow used:

- 64 rows × 64 columns;
- row stride 64;
- 8 heat-diffusion iterations;
- Reproducible numerical policy;
- worker budget 2;
- deterministic seed 170.

Both fresh processes produced:

- manifest SHA-256 `caa94172f51f4a161658ed39fff102340186ea6f3bba4f327a5a3fa2694e898c`;
- output digest `b10ced4bee0617873433aff5e2ea369135e9b48163924eb1ff249aec6756d3d3`;
- identical Runtime, profile, workload, and operation fingerprints;
- nine deterministic replays;
- zero adaptive cold starts and warm starts;
- zero learning, timing, holdout, drift, route-switch, and profile-mutation counters;
- `completion_status: passed`.

The Approved database hash was `28c7b7a94bcc4bb678225edff43193e60b535cf3c2d9e889251f79bff673e7c6` before and after replay.

![Cross-process stability](assets/benchmarks/windows-msvc-20260801/06_cross_process_stability.svg)

## Why this pilot matters

The pilot combines the v1.6 canonical stencil and norm plans with the v1.7 Runtime/profile trust model. It verifies more than numerical output: the selected routes, scheduler identities, worker budgets, profile hashes, experiment fields, telemetry invariants, and final manifest bytes all agree across fresh processes.

## Boundary

The proof applies only to the documented same-binary, same-architecture, same-floating-environment, exact-workload scope. It is not a cross-platform bitwise guarantee.

The retained evidence is available under [`assets/benchmarks/windows-msvc-20260801/cross-process/`](assets/benchmarks/windows-msvc-20260801/cross-process/).
