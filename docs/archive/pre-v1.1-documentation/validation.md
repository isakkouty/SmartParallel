# Validation

## Standard nested release gate

Windows:

```bat
scripts\validation\run_nested_release_validation.bat 31
scripts\validation\run_nested_release_validation.bat 3 trace
```

Linux/macOS:

```bash
./scripts/validation/run_nested_release_validation.sh 31
./scripts/validation/run_nested_release_validation.sh 3 trace
```

The script performs a clean Release build, runs all 12 CTest targets, and writes separate summary, raw-sample, and trace files.

## Required oneTBB gate

```bat
scripts\validation\run_nested_release_validation.bat 11 tbb
scripts\validation\run_nested_release_validation.bat 3 trace tbb
```

The TBB mode sets `SMARTPARALLEL_REQUIRE_TBB=ON`; configuration fails rather than silently validating a fallback backend. TBB output filenames are separate from ThreadPool output filenames.

## Production stress coverage

`smartparallel_nested_production_stress` validates:

- bounded profile, snapshot, and trace retention;
- active build/revalidation entries under eviction pressure;
- single-flight revalidation;
- policy drift and explicit callsite separation;
- randomized irregular trees across concurrent roots;
- repeated nested exception/recovery paths;
- StaticThread permit and partial-construction safety;
- near-`size_t` scheduler arithmetic;
- oneTBB arena-width enforcement when TBB is compiled in.

Additional release runs should include ASan/UBSan and ThreadSanitizer on a supported Linux compiler.
