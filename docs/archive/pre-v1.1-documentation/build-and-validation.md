# Building, installing, and validating SmartParallel

SmartParallel provides named CMake presets so routine builds do not require long
lists of `-D` options. The presets retain the fine-grained options for targeted
development while providing short commands for normal workflows.

## Requirements

- CMake 3.20 or newer
- A C++17 compiler
- oneTBB
- OpenCV `core` and `imgproc` components only when building the OpenCV benchmarks

The repository includes `vcpkg.json`. When using vcpkg, set the toolchain once in
your user environment or place it in a local `CMakeUserPresets.json`; do not hard-code
a machine-specific vcpkg path in the shared project presets.

## Cmake Integration
find_package(SmartParallel CONFIG REQUIRED)
target_link_libraries(MyApp PRIVATE SmartParallel::smart_parallel)

## Preset builds

List the available presets:

```text
cmake --list-presets
```

Build only the release library:

```text
cmake --preset release
cmake --build --preset release
```

Build the example:

```text
cmake --preset examples
cmake --build --preset examples
```

Build and run validation:

```text
cmake --preset validation
cmake --build --preset validation
ctest --preset validation
```

Build all benchmarks:

```text
cmake --preset benchmarks
cmake --build --preset benchmarks
```

Build every optional target:

```text
cmake --preset all
cmake --build --preset all
ctest --preset all
```

Each preset uses its own directory under `build/`, so configurations do not
overwrite one another.

## Umbrella options

For custom configurations, the main options are:

| Option | Purpose |
| --- | --- |
| `SMARTPARALLEL_BUILD_EXAMPLES` | Build the minimal example. |
| `SMARTPARALLEL_BUILD_VALIDATION` | Build all validation and hardening targets. |
| `SMARTPARALLEL_BUILD_BENCHMARKS` | Build all OpenCV, scientific, overhead, and decision-quality benchmarks. |
| `SMARTPARALLEL_BUILD_ALL` | Enable examples, validation, and benchmarks. |
| `SMARTPARALLEL_INSTALL` | Generate install and package targets; enabled by default. |

The original fine-grained options remain available when only one test or
benchmark is needed.

## Installing SmartParallel

Configure and build the release library, then install it into a chosen prefix:

```text
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix path/to/smartparallel-install
```

The installation contains:

```text
include/smart/
lib/
lib/cmake/SmartParallel/
```

On platforms where the library directory is named differently, CMake uses the
platform-standard GNU install directory.

## Consuming the installed package

A downstream project can use the exported target:

```cmake
find_package(SmartParallel CONFIG REQUIRED)

target_link_libraries(
  my_application
  PRIVATE SmartParallel::smart_parallel
)
```

Configure the downstream project with `CMAKE_PREFIX_PATH` pointing to the install
prefix, or install SmartParallel into a prefix already searched by CMake.

SmartParallel's package configuration locates oneTBB automatically through
`find_dependency(TBB CONFIG)`.

## Fine-grained targeted build

The compatibility options from earlier project versions are still supported.
For example:

```text
cmake -S . -B build/decision-quality \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMARTPARALLEL_BUILD_DECISION_QUALITY_AUDIT=ON
```

This is useful for automation and individual benchmark development. Normal users
should prefer the named presets.

## Windows runtime handling

For source-tree executables, the build copies the oneTBB runtime beside generated
executables on Windows. This helper is applied consistently to examples,
validation executables, and benchmarks.

Installed SmartParallel packages express oneTBB as a package dependency rather
than embedding a machine-specific runtime location. The consuming application is
responsible for deploying its oneTBB runtime according to its package manager or
installer policy.
