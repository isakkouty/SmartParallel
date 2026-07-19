# Examples

## Independent output elements

```cpp
smart::parallel_for(
    0,
    values.size(),
    [&](std::size_t index)
    {
        values[index] = std::sin(values[index]);
    });
```

## Two-dimensional image flattened to an index range

```cpp
const std::size_t pixels = width * height;

smart::parallel_for(
    0,
    pixels,
    [&](std::size_t index)
    {
        const std::size_t y = index / width;
        const std::size_t x = index % width;
        output[index] = input[index] > threshold ? 255 : 0;
    });
```

## Force oneTBB during controlled measurement

```cpp
const auto saved_engine = smart::global_config().execution_engine;
smart::global_config().execution_engine = smart::ExecutionEngineType::OneTbb;

smart::parallel_for(0, count, callback);

smart::global_config().execution_engine = saved_engine;
```

Use scoped restoration in production tests so an exception cannot leave global configuration modified.

## Memory-access hints

```cpp
smart::ExecutionHints hints = smart::pointer_chasing(
    working_set_bytes,
    dependent_accesses_per_iteration);
```

Hints are advanced evidence for the decision model. They should describe actual callback behavior and should not be fabricated merely to force a backend.
