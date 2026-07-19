# Execution backends

## Sequential

Runs the range directly on the caller thread. It has the lowest setup cost and is the correct choice when useful work is too small to amortize parallel overhead.

## ThreadPool

Uses SmartParallel's persistent global worker pool. Workers claim chunks through an atomic cursor. It avoids repeatedly creating operating-system threads and offers direct control over queue behavior.

Best fit: frequently submitted jobs, application-controlled worker ownership, and experiments requiring a SmartParallel-managed pool.

## StaticThread

Creates a fixed number of `std::thread` workers and assigns each one a contiguous range. It minimizes scheduling bookkeeping but creates and joins threads for each call.

Best fit: regular, balanced work where deterministic contiguous partitioning outweighs thread-creation cost. Automatic selection is disabled by default in v1.

## oneTBB

Executes a `tbb::parallel_for` inside a `tbb::task_arena` limited to the requested worker count. A zero SmartParallel chunk size maps to the backend's fine-grained default behavior.

Best fit: medium and large workloads, irregular callbacks, nested task graphs, and cases requiring dynamic load balancing.

## Explicit engine selection

```cpp
smart::global_config().execution_engine = smart::ExecutionEngineType::OneTbb;
```

Available values are `Auto`, `ThreadPool`, `StaticThread`, and `OneTbb`. `Auto` enables adaptive candidate selection.

## Future candidates

OpenMP would add a useful static/dynamic/guided loop runtime. SYCL or CUDA would require a separate device-compatible callback contract. `std::execution` is better treated as a compatibility backend because implementation behavior varies across standard libraries.
