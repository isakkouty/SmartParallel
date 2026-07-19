# Roadmap

## v1.0 release hardening

- Freeze `parallel_for` behavior except confirmed bug fixes.
- Add CMake install/export configuration and downstream `find_package` validation.
- Add Windows, GCC, and Clang CI.
- Archive a reproducible release benchmark environment and CSV baseline.
- Clarify public versus internal headers.

## v1.x scheduler improvements

- Tune tiny-workload thresholds and cold-call overhead.
- Evaluate oneTBB partitioner and grain-policy categories.
- Improve worker limits for memory-bandwidth-bound workloads.
- Expand forced-backend decision auditing to ThreadPool and StaticThread.
- Add per-call diagnostics rather than only global last-report access.

## Additional algorithms

- `parallel_reduce`
- `parallel_transform`
- `parallel_transform_reduce`
- scan and sort primitives

## Additional execution environments

- OpenMP for a distinct static/dynamic/guided CPU runtime.
- NUMA-aware policies on suitable hardware.
- SYCL or CUDA through a separate device-compatible callable contract.

The project should continue to add policy value above mature runtimes rather than attempting to replace oneTBB's generic task scheduler.
