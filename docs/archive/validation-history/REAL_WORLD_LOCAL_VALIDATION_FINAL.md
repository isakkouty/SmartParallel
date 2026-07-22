# Local Validation: High-Confidence Finishing Pass

## Toolchains

- GCC 14.2.0 Release
- Clang 17 Release focused build
- GCC AddressSanitizer + UndefinedBehaviorSanitizer focused build

## Results

- Core/validation build: PASS
- CTest: 15/15 PASS
- `smartparallel_real_world_optimization_hardening`: PASS under GCC
- `smartparallel_real_world_optimization_hardening`: PASS under Clang
- ASan + UBSan focused run: PASS
- LZ4 real-world target: compiled and executed locally
- BVH real-world target: compiled and executed locally
- Particle real-world target: compiled and executed locally
- Targeted BVH automatic runs: correctness PASS, L1 frontier confirmed
- Targeted particle tiny: `tiny_work_absolute_bypass` confirmed
- Batched CPU metric: plausible on substantial cases; short batches marked unavailable

## Local dependency limits

OpenCV and real oneTBB were unavailable in this Linux environment. Their C++
paths remain part of the unchanged Windows/vcpkg complete command and must be
validated by the supplied full run.
