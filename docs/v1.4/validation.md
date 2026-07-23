# v1.4 validation

The deterministic target is:

```text
smartparallel_v140_parallel_algorithms_validation
```

It validates all APIs under automatic selection, forced ThreadPool, and forced StaticThread, plus forced oneTBB when compiled in.

## Correctness coverage

- empty and single-element ranges;
- exactly-once elementwise execution;
- unary, binary, and in-place transforms;
- copy, fill, and indexed generate;
- ordered associative reductions;
- unary and binary transform-reduce;
- exact count and count-if results;
- predicate identities and short-circuit results;
- earliest-match search;
- exception propagation and cleanup;
- nested algorithm calls under the existing execution session;
- installed-package consumption through `SmartParallel::smart_parallel`.

## Hot-dispatch coverage

Validation also exercises scheduler-approved direct sequential execution and the algorithm-level hot cache for reduce, count, search, predicates, and copy. Release checks verify that:

- one public invocation executes one route only;
- forced ThreadPool, StaticThread, and oneTBB modes remain forced;
- nested calls retain the existing nested runtime;
- cached direct paths preserve results and exception behavior;
- workload/configuration changes do not silently reuse an invalid plan.

## Completed local validation

During the v1.4 correction:

- GCC Release passed the complete 17-test deterministic suite;
- Clang Release passed the v1.4 algorithm validation;
- Clang AddressSanitizer and UndefinedBehaviorSanitizer passed the v1.4 validation;
- the installed CMake package consumer built and executed the public algorithm API;
- the accepted Windows/MSVC benchmark produced **80/80 correct/authenticated summary rows** and **560/560 correct/authenticated raw samples**.

The repository GitHub Actions matrix remains the authoritative cross-platform build/test gate after a branch is pushed. It covers Windows/MSVC, Linux/GCC, Linux/Clang, macOS/Apple Clang, oneTBB-enabled/disabled configurations, installation, and the external package consumer.

Performance benchmarks remain manual and are not executed on shared CI runners. See [benchmark results](benchmarks.md) and [benchmark reproduction](benchmark-reproduction.md).
