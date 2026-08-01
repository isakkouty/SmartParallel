# SmartParallel v1.6 numerical contract

## Public presets

```cpp
enum class NumericalPolicy
{
    Fast,
    Reproducible,
    Accurate
};

struct NumericalOptions
{
    NumericalPolicy policy = NumericalPolicy::Fast;
};
```

| Policy | Evaluation structure | Arithmetic intent |
|---|---|---|
| Fast | Existing adaptive/native route | Preserve established performance behavior |
| Reproducible | Fixed operation-specific plan | Bitwise repeatability under the declared compatibility scope |
| Accurate | Fixed operation-specific plan | Reproducible plus a stronger algorithm where SmartParallel implements one |

Accurate is not a universal property that can be invented for any user callback. It is available only for recognized operations with a defined stronger method.

## Exact reproducibility scope

Reproducible and Accurate operations are bitwise repeatable across supported SmartParallel worker counts and eligible Native scheduler engines when all of the following remain unchanged:

- executable binary;
- CPU architecture;
- floating-point environment;
- input representation and bit pattern;
- numerical policy;
- operation semantic version;
- canonical plan version;
- documented compiler configuration.

The guarantee does not cover different architectures, compilers, binaries, optimization flags, unsafe fast-math modes, rounding modes, semantic versions, or arbitrary input NaN payload bits.

## Operation mapping

| Operation | Fast | Reproducible | Accurate |
|---|---|---|---|
| floating sum | retained native reduction | `canonical-pairwise-v1-leaf1024` | `canonical-neumaier-v1-leaf1024` |
| dot product | adaptive native partial sums | fixed product leaves and pairwise tree | fixed compensated-product leaves and merge |
| norm | native square/sum/square-root | canonical square/sum tree | `canonical-scaled-sumsq-v1-leaf1024` |
| AXPY | adaptive pointwise chunks | `canonical-pointwise-v1-target4096` | same fixed pointwise expression as Reproducible |
| stencil 2D | adaptive row chunks | `canonical-pointwise-2d-v1-target4096` | same fixed pointwise expression as Reproducible |

Accurate AXPY and stencil intentionally do not claim a stronger local arithmetic method. They share the Reproducible fixed-expression contract because there is no meaningful generic compensated version of those elementwise formulas in v1.6.

## Generic reductions

- Existing generic reduction overloads remain Fast.
- Reproducible generic reductions can fix evaluation order but do not make a mathematically non-associative operation associative.
- Accurate generic custom operations are unsupported unless SmartParallel recognizes the operation and implements its stronger arithmetic.
- Unsupported Accurate combinations fail clearly and never degrade silently.

## Pointwise determinism

For AXPY and stencil, scheduler order does not affect a result when:

- every logical output element is written once;
- input/output alias rules are satisfied;
- the expression order inside each element is fixed;
- the binary and floating-point environment remain unchanged.

The deterministic pointwise plan fixes tile boundaries independently of worker count, but workers may execute tiles concurrently in any order.
