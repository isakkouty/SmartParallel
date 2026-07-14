# Phase 8 Step 1: memory-bandwidth saturation calibration

This step targets high-regret contiguous memory workloads, especially large
streams and large-record scans. Those workloads often stop scaling before all
logical CPUs are occupied because the memory channels, cache hierarchy, and
page subsystem become the bottleneck.

The predictor now computes a bounded saturation correction from:

- workload-family confidence;
- working-set pressure relative to L3;
- represented bytes per logical iteration;
- physical and logical core counts;
- candidate worker count and backend type.

Candidates beyond the estimated bandwidth-saturation worker count receive an
increasing cost correction. Static-thread plans receive a small additional
penalty under strong saturation, while oneTBB receives a small bounded discount
for adaptive partitioning. The correction affects only workloads classified as
`StreamingMemory`; irregular-memory behavior remains unchanged for the next
step.

Diagnostics are exposed in `PlanCostEstimate` through the
`memory_bandwidth_*` fields. The feature can be disabled with
`Config::enable_memory_bandwidth_calibration`.
