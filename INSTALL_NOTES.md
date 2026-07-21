# Build and install notes

This release package intentionally omits the old generated `install/` directory and all local build directories. Those files were compiler- and machine-specific and could contain headers or binaries older than the updated source.

Create a clean install from the repository root:

```bash
cmake -S . -B build_install -DCMAKE_BUILD_TYPE=Release
cmake --build build_install --config Release
cmake --install build_install --config Release --prefix install
```

For a ThreadPool-only build without oneTBB:

```bash
cmake -S . -B build_install -DCMAKE_BUILD_TYPE=Release -DSMARTPARALLEL_ENABLE_TBB=OFF
cmake --build build_install --config Release
cmake --install build_install --config Release --prefix install
```

## Nested v1.1 release validation

Windows:

```bat
scripts\validation\run_nested_release_validation.bat 31
scripts\validation\run_nested_release_validation.bat 3 trace
```

Linux/macOS:

```bash
chmod +x scripts/validation/run_nested_release_validation.sh
./scripts/validation/run_nested_release_validation.sh 31
./scripts/validation/run_nested_release_validation.sh 3 trace
```

The validation runner uses a dedicated clean build directory and disables oneTBB so the core nested scheduler can be tested without an external backend dependency.
