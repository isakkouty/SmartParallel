# Changelog

All notable project changes should be recorded here.

## Beta 1.0

### Added

- Adaptive `for_each`, `parallel_for`, and `for_each_pair` APIs
- Workload builders, analysis, fingerprints, and hardware characteristics
- Runtime function profiler and profile cache
- Composite decision-provider system and execution reports
- Sequential, static-thread, thread-pool, and oneTBB execution paths
- Timing diagnostics and experience database persistence
- Ten standardized benchmarks
- CSV result export and one-command plot generation
- Root, architecture, API, roadmap, contribution, and benchmark documentation

### Changed

- Reorganized public headers into subsystem directories
- Standardized benchmark console output and CSV schemas
- Separated SmartParallel execution time from framework overhead in benchmark reports

### Known limitations

- Very small workloads expose fixed profiling and decision overhead
- Very cheap memory-streaming operations may be parallelized too early
- Adaptive worker count and grain-size selection are not complete
- Windows is the primary validated platform
