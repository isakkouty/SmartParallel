#pragma once

#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

#include <smart/core/safe_arithmetic.hpp>
#include <smart/workload/workload.hpp>

namespace smart
{
    namespace detail
    {
        template <typename T, typename = void>
        struct has_index_operator : std::false_type
        {
        };

        template <typename T>
        struct has_index_operator<T, std::void_t<
            decltype(std::declval<T&>()[std::size_t{}])>> : std::true_type
        {
        };

        template <typename T, typename = void>
        struct has_compatible_data : std::false_type
        {
        };

        template <typename T>
        struct has_compatible_data<T, std::void_t<
            decltype(std::data(std::declval<T&>()))>>
            : std::bool_constant<
                std::is_pointer_v<decltype(std::data(std::declval<T&>()))>>
        {
        };

        template <typename Container>
        Dimension describe_dimension(Container& container)
        {
            using ContainerType = std::remove_reference_t<Container>;
            using ValueType = typename ContainerType::value_type;
            using Reference = decltype(container[std::size_t{}]);

            Dimension dimension;
            dimension.size = static_cast<std::size_t>(container.size());
            dimension.object_size = sizeof(ValueType);

            // Indexed-access capability is a container property, not an
            // observation of the callback's memory-access pattern. A vector
            // can be traversed sequentially, randomly, or indirectly, so the
            // semantic access pattern remains unknown unless supplied by an
            // explicit hint or a runtime observation.
            dimension.random_access = false;
            dimension.random_access_known = false;

            constexpr bool proxy_reference =
                !std::is_same_v<
                    std::remove_cv_t<std::remove_reference_t<Reference>>,
                    ValueType>;

            if constexpr (proxy_reference)
            {
                dimension.storage_kind = StorageKind::ProxyReference;
                dimension.contiguous_known = false;
                dimension.stride_known = false;
            }
            else if constexpr (has_compatible_data<ContainerType>::value)
            {
                dimension.storage_kind = StorageKind::Contiguous;
                dimension.contiguous = true;
                dimension.contiguous_known = true;
                dimension.stride_bytes = sizeof(ValueType);
                dimension.stride_known = true;
            }
            else
            {
                dimension.storage_kind = StorageKind::Unknown;
                dimension.contiguous_known = false;
                dimension.stride_known = false;
            }

            return dimension;
        }
    }

    struct WorkloadBuilder
    {
        static Workload index_range(std::size_t iterations)
        {
            Workload workload;
            workload.kind = WorkloadKind::IndexRange;
            workload.iterations = iterations;

            Dimension dimension;
            dimension.size = iterations;
            dimension.object_size = 0;
            dimension.storage_kind = StorageKind::IndexGenerated;
            // An index range generates indices; it does not describe the
            // memory-access pattern performed by the callback.
            dimension.random_access = false;
            dimension.random_access_known = false;
            dimension.contiguous_known = false;
            dimension.stride_known = false;
            workload.dimensions.push_back(dimension);

            return workload;
        }

        template <typename Container>
        static Workload container(Container& container)
        {
            Workload workload;
            workload.kind = WorkloadKind::Container;
            workload.iterations =
                static_cast<std::size_t>(container.size());
            workload.dimensions.push_back(
                detail::describe_dimension(container));
            return workload;
        }

        template <typename ContainerA, typename ContainerB>
        static Workload pair_container(ContainerA& a, ContainerB& b)
        {
            Workload workload;
            workload.kind = WorkloadKind::MultiDimensional;

            const std::size_t size_a =
                static_cast<std::size_t>(a.size());
            const std::size_t size_b =
                static_cast<std::size_t>(b.size());

            const SizeCalculation iteration_count =
                saturating_multiply(size_a, size_b);

            workload.iterations = iteration_count.value;
            workload.iterations_saturated = iteration_count.saturated;
            workload.dimensions.push_back(detail::describe_dimension(a));
            workload.dimensions.push_back(detail::describe_dimension(b));
            return workload;
        }
    };
}
