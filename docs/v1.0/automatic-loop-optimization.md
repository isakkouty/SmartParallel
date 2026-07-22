# SmartParallel v1.0 — automatic loop optimization

> **Archived:** this describes the v1.0 product focus.

The v1.0 API accepted an index range and callback through `smart::parallel_for`. The runtime estimated callback cost, analyzed the workload, generated sequential and parallel candidates, and selected a plan using analytical and historical evidence.

The key contribution was policy automation: applications could avoid hard-coding one backend and chunking strategy for every loop. Runtime observations were retained in process memory and reused for repeated callsites. The scheduler could still be suboptimal for tiny or changing workloads, and v1.0 did not coordinate independent nested teams under a shared budget.

v1.1 retains this automatic loop optimization and adds nested execution coordination.
