# Migrating to SmartParallel v1.6

Existing code requires no change:

```cpp
auto result = smart::parallel_reduce(first, last, 0.0); // retained Fast behavior
```

Opt into an explicit contract per call:

```cpp
auto reproducible = smart::parallel_reduce(
    first, last, 0.0,
    smart::NumericalOptions{smart::NumericalPolicy::Reproducible});
```

For supported accurate arithmetic:

```cpp
auto accurate = smart::parallel_reduce(
    first, last, 0.0,
    smart::NumericalOptions{smart::NumericalPolicy::Accurate});
```

Do not place numerical policy in `global_config()`. v1.7 Runtime instances are planned to own defaults while preserving explicit per-operation overrides.

Custom generic reductions retain their associativity requirements. Reproducible fixes evaluation order but cannot make a non-associative semantic operation mathematically associative. Accurate custom operations are rejected unless SmartParallel recognizes and implements the stronger method.
