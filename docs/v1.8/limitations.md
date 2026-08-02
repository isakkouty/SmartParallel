# SmartParallel v1.8 — Known limitations

- Governance covers participating SmartParallel execution paths only.
- Unrelated application threads and external processes are outside the budget.
- oneTBB task arenas are upper bounds, not private worker ownership.
- Direct OpenCV calls outside SmartParallel are not governed.
- OpenCV containment serializes process-global provider configuration.
- Deterministic execution does not imply deterministic queue timing.
- Queue wait times are volatile diagnostics.
- Concurrent public sibling partitioning is not exposed in v1.8.
- Windows systems spanning multiple processor groups receive a conservative capacity diagnostic unless one safe process mask represents the effective set.
- CPU governance does not include memory, NUMA, affinity placement, GPUs, MPI, or distributed resources.
- No hard real-time guarantee or safety certification is provided.
- Performance evidence is machine-specific and can be negative or statistically inconclusive.
