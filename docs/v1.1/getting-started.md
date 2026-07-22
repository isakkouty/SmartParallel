# Getting started

> **Runtime documentation:** SmartParallel v1.1 behavior, retained by the current [v1.3 portability release](../v1.3/README.md).

## Requirements

- CMake 3.20 or newer
- A C++17 compiler
- oneTBB when the oneTBB backend is enabled

## Build the release library

```text
cmake --preset release
cmake --build --preset release
```

The preset writes to `build/release` and builds the installable `smart_parallel` library target.

## Use `parallel_for`

```cpp
#include <smart/execution/parallel.hpp>

#include <cstddef>
#include <vector>

std::vector<float> output(500'000);

smart::parallel_for(
    std::size_t{0},
    output.size(),
    [&](std::size_t i)
    {
        output[i] = static_cast<float>(i) * 0.5f;
    });
```

The range is half-open: `[begin, end)`. Empty ranges perform no work.

## Use nested loops

```cpp
smart::parallel_for(0, rows, [&](std::size_t row)
{
    smart::parallel_for(0, columns, [&](std::size_t column)
    {
        process(row, column);
    });
});
```

The inner call inherits the root session and budget. SmartParallel selects a bounded parallel frontier rather than allowing every level to recruit a full team.

## Next steps

- [Installation and package consumption](installation.md)
- [API reference](api.md)
- [Nested execution model](nested-parallelism.md)
- [Configuration](configuration.md)
- [Diagnostics](diagnostics.md)
