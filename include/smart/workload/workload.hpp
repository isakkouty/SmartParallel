#pragma once

#include <cstddef>
#include <vector>

namespace smart
{
    enum class WorkloadKind
    {
        IndexRange,
        Container,
        MultiDimensional
    };

    struct Dimension
    {
        std::size_t size = 0;
        std::size_t object_size = 0;
    };

    struct Workload
    {
        WorkloadKind kind = WorkloadKind::IndexRange;
        std::size_t iterations = 0;
        std::vector<Dimension> dimensions;
    };
}
