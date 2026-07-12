# Benchmark Suite

The benchmark suite evaluates two separate questions:

1. How do several direct execution approaches perform for a workload?
2. How close does SmartParallel come to the fastest measured approach after including its own profiling and decision overhead?

## Compared implementations

Most execution benchmarks compare:

- sequential loop
- raw `std::thread` chunking
- SmartParallel static-thread execution
- direct oneTBB execution
- SmartParallel automatic execution

The function-profiler benchmark measures the profiler itself rather than these five implementations.

## Reported values

| Field | Meaning |
|---|---|
| `smart_total_ms` | Total timed SmartParallel pipeline |
| `smart_execution_ms` | Selected execution phase only |
| `smart_overhead_ms` | Non-execution timing phases |
| `difference_ms` | SmartParallel total minus best measured total |
| `smart_gap_percent` | Difference divided by the best measured total |

Percentage gaps can look enormous for sub-microsecond sequential loops. In those cases the absolute difference and selected plan are more informative than the percentage.

## Reproduce figures

```powershell
py -m pip install pandas matplotlib numpy
py benchmarks\plot_all.py
```

Figures are generated under each benchmark's `images/beta_1_0/` folder.

## Benchmark index

| Benchmark | Main question |
|---|---|
| [Small workloads](small_workloads/README.md) | Does the framework avoid expensive threading for tiny loops? |
| [Compute heavy](compute_heavy/README.md) | Does expensive uniform work justify parallel execution? |
| [Tiny vs heavy](tiny_vs_heavy/README.md) | Does function cost change the decision at the same sizes? |
| [Mixed workload](mixed_workload/README.md) | How does the model behave with moderate variable iteration cost? |
| [Nested loops](nested_loops/README.md) | How does it handle a flattened regular workload? |
| [Pair workloads](pair_workloads/README.md) | How does it handle a Cartesian-product iteration space? |
| [Irregular workload](irregular_workload/README.md) | Does dynamic scheduling handle strongly variable work? |
| [Memory bandwidth](memory_bandwidth/README.md) | What happens for very cheap streaming transforms? |
| [Function profiler](function_profiler/README.md) | Does profiling rank cheap, medium, heavy, and irregular functions sensibly? |
| [Engineering mesh](engineering_mesh/README.md) | How does the framework behave on a more realistic geometry workload? |

## Interpretation summary

- Compute-heavy, pair, irregular, and engineering workloads generally converge close to direct oneTBB as total work grows.
- Tiny loops expose the fixed cost of adaptive analysis and profiling.
- Function profiling separates cheap and expensive kernels and identifies unstable irregular samples.
- Memory bandwidth is a known Beta 1.0 weakness: SmartParallel can switch to parallel execution before it becomes profitable and its selected execution path can trail direct oneTBB.

## Methodology limitations

The committed results are measurements from one development environment. They are not universal performance claims. CPU frequency scaling, background tasks, thermal state, compiler version, and hardware topology affect timings. A stable release should add machine metadata, warm-up policy, medians, variance, and automated cross-machine regression runs.
