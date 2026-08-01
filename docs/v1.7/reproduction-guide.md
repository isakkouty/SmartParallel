# SmartParallel v1.7 release reproduction guide

The v1.7 release workflow validates the project as built, installed, consumed, replayed, packaged, extracted, and rebuilt.

## Supported commands

Windows Developer Command Prompt:

```bat
scripts\validation\run_v17_reproducible_runtime_release_validation.bat 31 full
```

Linux/macOS:

```sh
SMARTPARALLEL_BUILD_JOBS=2 sh scripts/validation/run_v17_reproducible_runtime_release_validation.sh 31 full
```

## Nine-stage release workflow

1. **Configure and build** a clean Release publication with validation, tools, benchmarks, examples, installation, oneTBB, and the configured Vision provider.
2. **Run the complete v1.0–v1.7 regression**.
3. **Generate v1.7 and retained v1.6 benchmark evidence** and apply full statistical gates only in publication mode.
4. **Validate documentation, install, and external consumers**, including dependency resolution and runtime libraries.
5. **Exercise installed calibration, approval, and cross-process replay**, verifying expected artifacts and unchanged Approved bytes.
6. **Validate dependency matrices**, including authenticated no-oneTBB/no-OpenCV and focused oneTBB + OpenCV configurations.
7. **Run extended compiler/sanitizer matrices** where the platform supports them.
8. **Create a deterministic source-only ZIP**, extract it into a short clean path, verify the embedded source manifest, and repeat focused tests, documentation, replay, and benchmark-smoke evidence.
9. **Publish the validation summary and source checksum** only after all prior stages succeed.

## Required review points

A release reviewer should confirm:

- no code is omitted from or unexpectedly added to the source manifest;
- installed executables can locate required runtime dependencies;
- external package consumers configure without relying on build-tree state;
- no-dependency matrices are authenticated from cache and compile definitions;
- smoke mode validates evidence shape without pretending to provide publication statistics;
- full mode accepts only `PASS` or honest `INCONCLUSIVE-PASS` objectives;
- two fresh replay manifests are byte-identical;
- the Approved profile is unchanged;
- the exact returned ZIP reproduces the focused release contract.

## Publication artifacts

The accepted Windows publication is summarized in [`assets/benchmarks/windows-msvc-20260801/`](assets/benchmarks/windows-msvc-20260801/README.md). The independent Linux/GCC reference is under [`assets/benchmarks/linux-gcc-20260801/`](assets/benchmarks/linux-gcc-20260801/README.md).

See [benchmark reproduction](benchmark-reproduction.md) for direct benchmark commands and [validation](validation.md) for the accepted matrix.
