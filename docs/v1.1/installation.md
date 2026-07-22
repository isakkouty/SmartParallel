# Installation and build

> **Runtime documentation:** SmartParallel v1.1 behavior, retained by the current [v1.3 portability release](../v1.3/README.md).

## Dependencies

The core library requires C++17, CMake 3.20+, and oneTBB when `SMARTPARALLEL_ENABLE_TBB=ON`. OpenCV and LZ4 are required only for their real-world benchmark targets. The included `vcpkg.json` lists the release benchmark dependencies.

## CMake presets

```text
cmake --list-presets
cmake --preset release
cmake --build --preset release
```

Other presets are `debug`, `examples`, `validation`, `benchmarks`, and `all`. Each uses a separate directory under `build/`.

## Windows with vcpkg

Set `VCPKG_ROOT` before configuring. The root CMake project automatically selects the vcpkg toolchain when no other toolchain was supplied.

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg"
cmake --preset release
cmake --build --preset release
```

For the complete real-world suite, use the dedicated NMake/MSVC script documented in [benchmark reproduction](benchmark-reproduction.md).

## Custom CMake configuration

```text
cmake -S . -B build/custom \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMARTPARALLEL_BUILD_EXAMPLES=ON
cmake --build build/custom
```

Primary umbrella options:

| Option | Purpose |
|---|---|
| `SMARTPARALLEL_BUILD_EXAMPLES` | Build examples. |
| `SMARTPARALLEL_BUILD_VALIDATION` | Build tests and validation targets. |
| `SMARTPARALLEL_BUILD_BENCHMARKS` | Build benchmark suites. |
| `SMARTPARALLEL_BUILD_ALL` | Build examples, validation, and benchmarks. |
| `SMARTPARALLEL_INSTALL` | Generate install/package targets; enabled by default. |
| `SMARTPARALLEL_ENABLE_TBB` | Enable oneTBB discovery and execution. |
| `SMARTPARALLEL_REQUIRE_TBB` | Fail configuration when oneTBB is required but unavailable. |

## Install

```text
cmake --install build/release --prefix path/to/smartparallel-install
```

## Consume from another CMake project

```cmake
find_package(SmartParallel CONFIG REQUIRED)

target_link_libraries(
    my_application
    PRIVATE SmartParallel::smart_parallel
)
```

Point `CMAKE_PREFIX_PATH` at the install prefix when it is not already searched by CMake.
