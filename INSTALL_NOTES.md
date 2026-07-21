# Build and install notes

This release package omits generated install trees and local build directories. Rebuild from source on the target machine.

```bash
cmake -S . -B build_install -DCMAKE_BUILD_TYPE=Release
cmake --build build_install --config Release
cmake --install build_install --config Release --prefix install
```

For a ThreadPool/StaticThread-only build without oneTBB:

```bash
cmake -S . -B build_install -DCMAKE_BUILD_TYPE=Release -DSMARTPARALLEL_ENABLE_TBB=OFF
cmake --build build_install --config Release
cmake --install build_install --config Release --prefix install
```

## Complete nested v1.1 release validation

Windows:

```bat
scripts\validation\run_nested_cross_backend_validation.bat 31
```

Linux/macOS:

```bash
chmod +x scripts/validation/*.sh scripts/validation/compare_nested_backend_results.py
./scripts/validation/run_nested_cross_backend_validation.sh 31
```

The cross-backend gate runs ThreadPool, StaticThread, and required real oneTBB in both performance and trace modes. It then verifies checksums, actual backend identity, root permit limits, and backend-specific helper behavior.

For a machine without oneTBB, run the ThreadPool or StaticThread gate individually:

```bat
scripts\validation\run_nested_release_validation.bat 31
scripts\validation\run_nested_release_validation.bat 31 static
```
