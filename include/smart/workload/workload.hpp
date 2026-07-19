#pragma once

#include <cstddef>
#include <smart/workload/observation.hpp>
#include <vector>

namespace smart
{
enum class WorkloadKind
{
    IndexRange,
    Container,
    MultiDimensional
};

enum class StorageKind
{
    Contiguous,
    Segmented,
    NodeBased,
    ProxyReference,
    IndexGenerated,
    Unknown
};

struct Dimension
{
    std::size_t size = 0;
    std::size_t object_size = 0;

    StorageKind storage_kind = StorageKind::Unknown;
    bool contiguous = false;
    bool contiguous_known = false;
    bool random_access = false;

    bool random_access_known = false;
    std::size_t stride_bytes = 0;
    bool stride_known = false;
};

struct Workload
{
    WorkloadKind kind = WorkloadKind::IndexRange;
    std::size_t iterations = 0;
    bool iterations_saturated = false;
    std::vector<Dimension> dimensions;
};
} // namespace smart
