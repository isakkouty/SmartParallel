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

## Real-world integration dependencies and validation

The real-world suite uses vcpkg manifest features and the existing Windows
NMake/MSVC build convention. Set `VCPKG_ROOT`; CMake installs `tbb`, `opencv4`,
and `lz4` automatically when the complete benchmark build is configured.

```bat
set VCPKG_ROOT=D:\Tools\vcpkg
scripts\benchmarks\build_real_world_benchmarks.bat
scripts\benchmarks\run_real_world_complete.bat 31
```

Manual dependency fallback:

```bat
%VCPKG_ROOT%\vcpkg.exe install tbb:x64-windows opencv4:x64-windows lz4:x64-windows
```

These dependencies are benchmark-only. A normal core build does not require
OpenCV or LZ4.
