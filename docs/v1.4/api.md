# v1.4 parallel algorithm API

Include:

```cpp
#include <smart/execution/algorithms.hpp>
```

All v1.4 algorithms require random-access iterators. Parallel routes schedule contiguous logical chunks through the existing `parallel_for` runtime, so nested calls inherit the active execution session and concurrency budget. For the cheap root automatic families, a learned direct-sequential decision can be applied before chunk construction; scheduler-approved sequential plans also use the same one-pass algorithm body. Public result and exception contracts do not change.

## Elementwise algorithms

```cpp
smart::parallel_for_each(first, last, function);

output_end = smart::parallel_transform(first, last, output, unary_operation);
output_end = smart::parallel_transform(first1, last1, first2, output, binary_operation);

output_end = smart::parallel_copy(first, last, output);
smart::parallel_fill(first, last, value);

smart::parallel_generate(first, last, [](std::size_t index) {
    return make_value(index);
});
```

`parallel_generate` intentionally uses an indexed generator. A shared stateful no-argument generator would either race or produce a sequence that depends on scheduling order.

The fill value is copied before parallel execution, so aliasing an element in the destination cannot change the value observed by other chunks. Exact in-place unary transform is supported. Other overlapping input/output ranges are not supported. Elementwise execution order is unspecified, and a thrown exception may leave a partially modified output range.

## Reductions

```cpp
auto sum = smart::parallel_reduce(first, last, initial, operation);
auto sum = smart::parallel_reduce(first, last, initial);

auto result = smart::parallel_transform_reduce(
    first, last, initial, reduction, transform);

auto dot = smart::parallel_transform_reduce(
    first1, last1, first2, initial, reduction, binary_transform);
```

The reduction operation must be associative. It does not have to be commutative: each chunk is reduced in input order and chunk partials are combined in chunk order. Parenthesization differs from a strictly sequential fold, so floating-point results may differ in their last bits.

Reduction operations and transforms must be copy constructible. Exceptions destroy temporary partials and propagate to the caller.

## Counting and predicates

```cpp
auto count = smart::parallel_count(first, last, value);
auto count = smart::parallel_count_if(first, last, predicate);

bool any  = smart::parallel_any_of(first, last, predicate);
bool all  = smart::parallel_all_of(first, last, predicate);
bool none = smart::parallel_none_of(first, last, predicate);
```

Counting results are exact. Predicate algorithms use best-effort short-circuiting: work that has already started may finish after the logical result is known. Predicate order and invocation count are unspecified.

## Search

```cpp
auto iterator = smart::parallel_find(first, last, value);
auto iterator = smart::parallel_find_if(first, last, predicate);
```

Search returns the earliest matching iterator or `last`. Chunks may be searched out of order, but an atomic lowest-index result preserves the public earliest-match contract. Some predicate work may continue after a candidate is found.

## Common callable contract

Algorithm callables must be copy constructible. SmartParallel keeps a separate callable copy for each logical chunk. Calls from different chunks may execute concurrently, so state referenced by a callable must not be mutated without synchronization. Exceptions are propagated through the existing SmartParallel cancellation and cleanup path.

For reusable functor types used at unrelated semantic callsites, `smart::with_parallel_callsite(key, functor)` remains available to separate runtime-learning identities.
