# Examples

> **Runtime documentation:** SmartParallel v1.1 behavior, retained by the current [v1.3 portability release](../v1.3/README.md).

## Flat loop

```cpp
smart::parallel_for(0, values.size(), [&](std::size_t i)
{
    values[i] = transform(values[i]);
});
```

## Nested loop

```cpp
smart::parallel_for(0, images.size(), [&](std::size_t image)
{
    smart::parallel_for(0, tiles_per_image, [&](std::size_t tile)
    {
        process_tile(images[image], tile);
    });
});
```

## Multidimensional loop

```cpp
std::array<std::size_t, 3> extents{depth, height, width};
smart::parallel_for_nd(extents, [&](const auto& index)
{
    process_voxel(index[0], index[1], index[2]);
});
```

## Distinguish reusable callsites

```cpp
auto first = smart::with_parallel_callsite(1, reusable_callback);
auto second = smart::with_parallel_callsite(2, reusable_callback);
smart::parallel_for(0, first_count, first);
smart::parallel_for(0, second_count, second);
```

## Force a backend for controlled measurement

```cpp
auto& config = smart::global_config();
config.execution_engine = smart::ExecutionEngineType::OneTbb;
smart::parallel_for(0, count, callback);
```

Do not mutate the global configuration concurrently. Restore or set the final configuration before starting worker threads.

The repository's `examples/` directory contains focused validation programs for contexts, budgets, backends, exceptions, deep nesting, and scheduler behavior. The corresponding Windows launchers are under `scripts/examples/`.
