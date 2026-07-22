# Native hardware discovery

SmartParallel uses `smart::hardware_characteristics()` to obtain advisory topology inputs for workload analysis and scheduling. v1.3 provides a native implementation on each supported operating system while retaining conservative defaults whenever a query is unavailable.

## Returned fields

```cpp
struct HardwareCharacteristics
{
    std::size_t logical_threads;
    std::size_t physical_cores;
    std::size_t numa_nodes;
    std::size_t page_size;
    std::size_t cache_line_size;
    std::size_t l1_cache_size;
    std::size_t l2_cache_size;
    std::size_t l3_cache_size;
    bool cache_info_available;
    bool numa_info_available;
    bool page_size_available;
};
```

The availability flags distinguish native observations from conservative defaults. Scheduler correctness never depends on an optional field being available.

## Windows

SmartParallel uses processor-group-aware `GetActiveProcessorCount`, `GetSystemInfo`, and `GetLogicalProcessorInformationEx`. This preserves the existing Windows cache and NUMA aggregation while supporting machines with more than one processor group.

## Linux

SmartParallel first respects the process CPU-affinity mask. It then reads topology and cache data for the CPUs available to that process:

- `/sys/devices/system/cpu/cpu*/topology`
- `/sys/devices/system/cpu/cpu*/cache`
- `/sys/devices/system/node/node*/cpulist`
- `_SC_PAGESIZE`

Physical cores are identified by package/die/core tuples. Cache entries shared by multiple logical CPUs are deduplicated. NUMA-node counts include only nodes intersecting the process CPU set. `/proc/cpuinfo`, online-processor counts, and standard C++ thread counts provide fallbacks.

This behavior is useful inside containers and CI runners because SmartParallel uses the CPU set actually granted to the process instead of assuming every host CPU is available.

## macOS

SmartParallel uses `sysctl` values exposed by macOS:

- `hw.logicalcpu`
- `hw.physicalcpu`
- `hw.pagesize`
- `hw.cachelinesize`
- `hw.l1dcachesize`
- `hw.l1icachesize`
- `hw.l2cachesize`
- `hw.l3cachesize`

Not every cache key exists on every Intel or Apple Silicon machine. Missing keys leave the corresponding value unavailable without failing initialization.

macOS does not provide a stable public NUMA topology API suitable for this library. SmartParallel therefore reports one conservative NUMA node and leaves `numa_info_available` false rather than inferring NUMA structure from CPU packages or performance/efficiency clusters.

## Lifetime and overhead

Linux sysfs and macOS `sysctl` discovery are performed once per process and cached. Hardware topology is treated as immutable during execution, avoiding operating-system queries in scheduling hot paths. Windows retains its established discovery behavior.

## Validation

The deterministic `smartparallel_hardware_characteristics_portability` CTest checks that:

- logical and physical counts are nonzero and internally consistent;
- page and cache-line values are plausible;
- available cache data contains at least one cache level;
- no optional discovery failure prevents normal execution.

The installed-package consumer also calls this API, so GitHub Actions validates that native discovery works after `cmake --install` and `find_package`, not only from the source tree.
