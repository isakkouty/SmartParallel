# Reproduction

From a clean source extraction, run:

```sh
sh scripts/validation/run_v18_governed_execution_release_validation.sh 31 full
```

On Windows, use a Visual Studio 2022 x64 Developer Command Prompt:

```bat
scripts\validation\run_v18_governed_execution_release_validation.bat 31 full
```

The workflows configure, build, run the complete regression, publish v1.8/v1.7/v1.6 benchmark evidence, validate documentation and installed consumers, exercise calibration and deterministic replay, run dependency and sanitizer/compiler matrices, create one deterministic source ZIP, extract it into a fresh directory, and run focused exact-archive tests and benchmark smoke.

The scripts stop on the first non-zero or negative process exit status. Generated validation output is excluded from the source archive.

## Final acceptance sequence

The retained Linux/GCC evidence is accepted. Run the Windows command above with `31 full` on the intended Windows publication host, retain the generated metrics and figures under a Windows-specific documentation asset directory, then rerun the exact returned ZIP checks. The analyzer emits a cross-platform comparison only when both raw datasets are supplied; it does not manufacture a placeholder comparison.
