# Cross-platform build and installation

> **Current release:** SmartParallel v1.3.0.

## Requirements

- CMake 3.20 or newer
- A C++17 compiler
- A platform thread implementation, discovered through CMake `Threads`
- Ninja for the repository presets
- oneTBB only when `SMARTPARALLEL_ENABLE_TBB=ON`

Validated CI compilers are MSVC on Windows, GCC and Clang on Linux, and Apple Clang on macOS.

## Build without oneTBB

```text
cmake -S . -B build/no-tbb -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSMARTPARALLEL_BUILD_VALIDATION=ON \
  -DSMARTPARALLEL_ENABLE_TBB=OFF
cmake --build build/no-tbb --parallel
ctest --test-dir build/no-tbb --output-on-failure
```

The equivalent preset workflow is:

```text
cmake --preset ci-debug-no-tbb
cmake --build --preset ci-debug-no-tbb
ctest --preset ci-debug-no-tbb
```

## Build with oneTBB through vcpkg

Clone and bootstrap vcpkg once on a developer machine, then keep `VCPKG_ROOT` set to that installation.

Windows:

```bat
git clone https://github.com/microsoft/vcpkg.git D:\Tools\vcpkg
D:\Tools\vcpkg\bootstrap-vcpkg.bat -disableMetrics
setx VCPKG_ROOT D:\Tools\vcpkg
```

Linux or macOS:

```text
git clone https://github.com/microsoft/vcpkg.git "$HOME/tools/vcpkg"
"$HOME/tools/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
echo 'export VCPKG_ROOT="$HOME/tools/vcpkg"' >> ~/.profile
```

After opening a new shell:

```text
cmake --preset ci-release-tbb
cmake --build --preset ci-release-tbb
ctest --preset ci-release-tbb
```

The repository manifest pins dependency versions through `builtin-baseline`. The real-world benchmark features are not installed unless explicitly requested.

## Install the package

```text
cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMARTPARALLEL_ENABLE_TBB=OFF \
  -DSMARTPARALLEL_INSTALL=ON
cmake --build build/release --parallel
cmake --install build/release --prefix build/install
```

Consume it from another CMake project:

```cmake
find_package(SmartParallel 1.3 CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE SmartParallel::smart_parallel)
```

Validate the installed package using the repository consumer:

```text
cmake -S tests/package-consumer -B build/package-consumer -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/build/install"
cmake --build build/package-consumer
ctest --test-dir build/package-consumer --output-on-failure
```

For a TBB-enabled installation, configure the consumer with the same vcpkg toolchain or another CMake prefix that provides `TBBConfig.cmake`.
