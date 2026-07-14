#pragma once

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

#include <smart/core/safe_arithmetic.hpp>
#include <smart/profiling/function_profiler.hpp>

namespace smart
{
    template <typename Container, typename Function>
    FunctionProfile profile_container_on_copies(
        const Container& container,
        const Function& function,
        const FunctionProfiler::Config& config)
    {
        using ContainerType = std::remove_reference_t<Container>;
        using ValueType = typename ContainerType::value_type;
        using Callable = std::decay_t<Function>;

        FunctionProfile profile;

        if (container.size() == 0)
        {
            profile.unavailable_reason =
                FunctionProfileUnavailableReason::EmptyRange;
            return profile;
        }

        if constexpr (!std::is_copy_constructible_v<ValueType>)
        {
            profile.unavailable_reason =
                FunctionProfileUnavailableReason::ValueNotCopyConstructible;
            return profile;
        }
        else if constexpr (!std::is_copy_constructible_v<Callable>)
        {
            profile.unavailable_reason =
                FunctionProfileUnavailableReason::CallableNotCopyConstructible;
            return profile;
        }
        else if constexpr (!std::is_invocable_v<Callable&, ValueType&>)
        {
            profile.unavailable_reason =
                FunctionProfileUnavailableReason::CallableNotInvocableOnCopy;
            return profile;
        }
        else
        {
            Callable profiling_function(function);
            FunctionProfiler profiler;

            profile = profiler.profile_index_range(
                0,
                static_cast<std::size_t>(container.size()),
                [&](std::size_t index)
                {
                    ValueType sample(container[index]);
                    std::invoke(profiling_function, sample);
                },
                config);

            if (profile.available)
            {
                profile.sampling_mode =
                    FunctionProfileSamplingMode::IsolatedCopies;
            }

            return profile;
        }
    }

    template <typename ContainerA, typename ContainerB, typename Function>
    FunctionProfile profile_pair_on_copies(
        const ContainerA& a,
        const ContainerB& b,
        const Function& function,
        const FunctionProfiler::Config& config)
    {
        using ContainerTypeA = std::remove_reference_t<ContainerA>;
        using ContainerTypeB = std::remove_reference_t<ContainerB>;
        using ValueTypeA = typename ContainerTypeA::value_type;
        using ValueTypeB = typename ContainerTypeB::value_type;
        using Callable = std::decay_t<Function>;

        FunctionProfile profile;

        if (a.size() == 0 || b.size() == 0)
        {
            profile.unavailable_reason =
                FunctionProfileUnavailableReason::EmptyRange;
            return profile;
        }

        if constexpr (
            !std::is_copy_constructible_v<ValueTypeA> ||
            !std::is_copy_constructible_v<ValueTypeB>)
        {
            profile.unavailable_reason =
                FunctionProfileUnavailableReason::ValueNotCopyConstructible;
            return profile;
        }
        else if constexpr (!std::is_copy_constructible_v<Callable>)
        {
            profile.unavailable_reason =
                FunctionProfileUnavailableReason::CallableNotCopyConstructible;
            return profile;
        }
        else if constexpr (
            !std::is_invocable_v<Callable&, ValueTypeA&, ValueTypeB&>)
        {
            profile.unavailable_reason =
                FunctionProfileUnavailableReason::CallableNotInvocableOnCopy;
            return profile;
        }
        else
        {
            const std::size_t size_b =
                static_cast<std::size_t>(b.size());

            const SizeCalculation total_calculation =
                saturating_multiply(
                    static_cast<std::size_t>(a.size()),
                    size_b);

            if (total_calculation.saturated)
            {
                profile.unavailable_reason =
                    FunctionProfileUnavailableReason::IterationCountOverflow;
                return profile;
            }

            const std::size_t total = total_calculation.value;

            Callable profiling_function(function);
            FunctionProfiler profiler;

            profile = profiler.profile_index_range(
                0,
                total,
                [&](std::size_t flat_index)
                {
                    const std::size_t index_a = flat_index / size_b;
                    const std::size_t index_b = flat_index % size_b;

                    ValueTypeA sample_a(a[index_a]);
                    ValueTypeB sample_b(b[index_b]);

                    std::invoke(
                        profiling_function,
                        sample_a,
                        sample_b);
                },
                config);

            if (profile.available)
            {
                profile.sampling_mode =
                    FunctionProfileSamplingMode::IsolatedCopies;
            }

            return profile;
        }
    }
}
