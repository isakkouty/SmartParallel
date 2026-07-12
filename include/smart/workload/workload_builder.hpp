#pragma once

#include <cstddef>

#include <smart/workload/workload.hpp>

namespace smart
{
    struct WorkloadBuilder
    {
        static Workload index_range(std::size_t iterations)
        {
            Workload workload;
            workload.kind = WorkloadKind::IndexRange;
            workload.iterations = iterations;
            return workload;
        }

        template <typename Container>
        static Workload container(Container& container)
        {
            Workload workload;
            workload.kind = WorkloadKind::Container;
            workload.iterations = static_cast<std::size_t>(container.size());

            Dimension dim;
            dim.size = container.size();

            if (container.size() > 0)
            {
                dim.object_size = sizeof(container[0]);
            }

            workload.dimensions.push_back(dim);

            return workload;
        }

        template <typename ContainerA, typename ContainerB>
        static Workload pair_container(ContainerA& a, ContainerB& b)
        {
            Workload workload;
            workload.kind = WorkloadKind::MultiDimensional;

            workload.iterations = a.size() * b.size();

            Dimension dim_a;
            dim_a.size = a.size();

            if (a.size() > 0)
            {
                dim_a.object_size = sizeof(a[0]);
            }

            Dimension dim_b;
            dim_b.size = b.size();

            if (b.size() > 0)
            {
                dim_b.object_size = sizeof(b[0]);
            }

            workload.dimensions.push_back(dim_a);
            workload.dimensions.push_back(dim_b);

            return workload;
        }
    };
}
