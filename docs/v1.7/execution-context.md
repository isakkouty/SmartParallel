# SmartParallel v1.7 ExecutionContext

`ExecutionContext` is the lightweight execution handle produced by a `Runtime`.

## Basic use

```cpp
smart::RuntimeOptions options;
options.worker_budget = 8;
smart::Runtime runtime(options);
auto context = runtime.context();

smart::parallel_for(context, std::size_t{0}, count, callback);
smart::linalg::axpy(context, y, alpha, x, numerical_options);
```

## What a context carries

A context carries shared ownership of Runtime state plus:

- the construction-time configuration identity;
- the existing nested-execution lineage;
- Runtime-scoped scheduler and profile access;
- an exact-plan override when Deterministic replay requires one;
- the Runtime’s default numerical policy.

Copying a context does not duplicate a pool, scheduler, profile database, or evidence store.

## Lifetime

A copied context remains usable after the original `Runtime` wrapper is destroyed because it retains a shared handle to the underlying state.

```cpp
auto make_context()
{
    smart::Runtime runtime;
    return runtime.context();
}

auto context = make_context(); // Runtime state remains alive.
```

## Supported context-aware operations

The release provides context-aware overloads for `parallel_for`, generic Fast `parallel_reduce`, AXPY, dot, norm, stencil 2D, and Vision threshold.

Named semantic operations can produce persistent exact identities. Arbitrary callbacks can still execute through a context, but their user-defined semantics are not automatically persisted across processes.

## Nesting

Nested calls inherit the active context and continue to use the existing root-session coordination, concurrency budgets, participant leases, helping behavior, and exception propagation. v1.7 does not create a second nesting model.
