# SmartParallel v1.6 scientific operations

The v1.6 scientific APIs are experimental. They operate on non-owning host-memory views and expose an explicit `NumericalOptions` argument.

## AXPY

```cpp
smart::linalg::axpy(y, alpha, x, options);
```

Computes:

```text
y[i] = alpha * x[i] + y[i]
```

Properties:

- `float` and `double`;
- contiguous or strided vectors;
- equal logical extents required;
- writable `y` and read-only `x`;
- exact same mapping supported;
- unsupported partial or ambiguous overlap rejected;
- Fast uses adaptive chunks;
- Reproducible and Accurate use `canonical-pointwise-v1-target4096`;
- Accurate intentionally shares the Reproducible per-element arithmetic.

## Dot product

```cpp
auto value = smart::linalg::dot(x, y, options);
```

Properties:

- `float` and `double`;
- contiguous or strided vectors;
- equal logical extents required;
- Reproducible uses fixed product leaves and a fixed pairwise merge tree;
- Accurate uses deterministic compensated product accumulation;
- empty input returns `+0`.

## Euclidean norm

```cpp
auto value = smart::linalg::norm(x, options);
```

Fast and Reproducible use their respective square/sum contracts followed by square root. Accurate uses a fixed scaled sum-of-squares state:

```text
maintain scale
maintain scaled sum of squares
merge states deterministically
return scale × sqrt(sum)
```

This avoids avoidable overflow or underflow from naïvely squaring extreme finite values.

## Five-point stencil

```cpp
smart::scientific::stencil_2d(
    input,
    output,
    coefficients,
    options);
```

For interior cells, semantic version `stencil-2d-v1` evaluates in this order:

```text
center × input(row, column)
+ north × input(row - 1, column)
+ south × input(row + 1, column)
+ west  × input(row, column - 1)
+ east  × input(row, column + 1)
```

Properties:

- `float` and `double`;
- contiguous or padded-row matrix views;
- matching input/output dimensions required;
- disjoint input and output required;
- boundary values copied unchanged;
- tiny grids where every element is boundary are supported;
- Fast uses adaptive row chunks;
- Reproducible and Accurate use worker-independent fixed row tiles under `canonical-pointwise-2d-v1-target4096`;
- Accurate shares the fixed pointwise expression and makes no stronger local-error claim.

## Validated kernel execution

The public views remain fully checked at construction and operation entry. After the operation has validated extents, address spans, unique writable mappings, and overlap rules, the inner scientific kernels use raw pointers plus the already validated element strides. This avoids repeated bounds and overflow checks inside every AXPY element, dot/norm leaf, or stencil neighbor access while preserving the same public safety contract.

Contiguous inputs use dedicated unit-stride loops that compilers can vectorize. Non-contiguous vector and matrix layouts retain explicit stride-aware loops. The deterministic numerical plan controls decomposition and arithmetic order; the validated pointer kernel only changes how already-approved addresses are reached.

## Authentication

After a policy-aware operation, `last_numerical_execution_report()` exposes the requested policy, plan, arithmetic method, selected scheduler, worker count, and capability result. Benchmarks use this report to prove that the requested numerical route actually executed.
