#pragma once

#include <smart/numerical/detail/canonical_reduction.hpp>

#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace smart::detail
{
template <typename Operation>
struct is_std_plus : std::false_type {};

template <typename T>
struct is_std_plus<std::plus<T>> : std::true_type {};

template <typename Operation>
inline constexpr bool is_std_plus_v = is_std_plus<std::decay_t<Operation>>::value;

template <typename Operation>
struct is_std_multiplies : std::false_type {};

template <typename T>
struct is_std_multiplies<std::multiplies<T>> : std::true_type {};

template <typename Operation>
inline constexpr bool is_std_multiplies_v = is_std_multiplies<std::decay_t<Operation>>::value;
}

namespace smart
{
template <typename InputIterator, typename T, typename BinaryOperation>
T parallel_reduce(InputIterator first,
                  InputIterator last,
                  T init,
                  BinaryOperation binary_operation,
                  NumericalOptions options)
{
    if (options.policy == NumericalPolicy::Fast)
    {
        const bool has_work = first != last;
        T result = parallel_reduce(first, last, std::move(init), std::move(binary_operation));
        detail::authenticate_numerical_execution("parallel_reduce",
                                                 options.policy,
                                                 detail::AccumulationMethod::Native,
                                                 "none", true, has_work);
        return result;
    }

    const std::size_t total = detail::algorithm_range_size(first, last, "parallel_reduce");
    const auto identity = [](const auto& value) -> T { return static_cast<T>(value); };
    const std::size_t callsite = detail::combine_hash(
        detail::algorithm_identity(detail::ParallelAlgorithmKind::Reduce,
                                   binary_operation,
                                   identity),
        static_cast<std::size_t>(options.policy));

    if (options.policy == NumericalPolicy::Accurate)
    {
        if constexpr (std::is_floating_point_v<T> && detail::is_std_plus_v<BinaryOperation>)
        {
            detail::require_numerical_capability(
                detail::native_compensated_reduction_capabilities, options.policy);
            T result = detail::canonical_compensated_reduce(
                first, total, init, identity, callsite);
            detail::authenticate_numerical_execution("parallel_reduce",
                                                     options.policy,
                                                     detail::AccumulationMethod::Compensated,
                                                     detail::compensated_plan_v1,
                                                     true, total != 0);
            return result;
        }
        else
        {
            throw std::invalid_argument(
                "SmartParallel Accurate parallel_reduce supports floating-point std::plus only");
        }
    }

    detail::require_numerical_capability(
        detail::native_pairwise_reduction_capabilities, options.policy);
    T result = detail::canonical_transform_reduce(first,
                                                  total,
                                                  std::move(init),
                                                  std::move(binary_operation),
                                                  identity,
                                                  callsite);
    detail::authenticate_numerical_execution("parallel_reduce",
                                             options.policy,
                                             detail::AccumulationMethod::CanonicalPairwise,
                                             detail::canonical_pairwise_plan_v1,
                                             true, total != 0);
    return result;
}

template <typename InputIterator, typename T>
T parallel_reduce(InputIterator first,
                  InputIterator last,
                  T init,
                  NumericalOptions options)
{
    return parallel_reduce(first, last, std::move(init), std::plus<>{}, options);
}

template <typename InputIterator,
          typename T,
          typename BinaryOperation,
          typename UnaryOperation>
T parallel_transform_reduce(InputIterator first,
                            InputIterator last,
                            T init,
                            BinaryOperation binary_operation,
                            UnaryOperation unary_operation,
                            NumericalOptions options)
{
    if (options.policy == NumericalPolicy::Fast)
    {
        const bool has_work = first != last;
        T result = parallel_transform_reduce(first,
                                             last,
                                             std::move(init),
                                             std::move(binary_operation),
                                             std::move(unary_operation));
        detail::authenticate_numerical_execution("parallel_transform_reduce",
                                                 options.policy,
                                                 detail::AccumulationMethod::Native,
                                                 "none", true, has_work);
        return result;
    }

    const std::size_t total = detail::algorithm_range_size(
        first, last, "parallel_transform_reduce");
    const std::size_t callsite = detail::combine_hash(
        detail::algorithm_identity(detail::ParallelAlgorithmKind::TransformReduce,
                                   binary_operation,
                                   unary_operation),
        static_cast<std::size_t>(options.policy));

    if (options.policy == NumericalPolicy::Accurate)
    {
        if constexpr (std::is_floating_point_v<T> && detail::is_std_plus_v<BinaryOperation>)
        {
            detail::require_numerical_capability(
                detail::native_compensated_reduction_capabilities, options.policy);
            T result = detail::canonical_compensated_reduce(
                first, total, init, unary_operation, callsite);
            detail::authenticate_numerical_execution("parallel_transform_reduce",
                                                     options.policy,
                                                     detail::AccumulationMethod::Compensated,
                                                     detail::compensated_plan_v1,
                                                     true, total != 0);
            return result;
        }
        else
        {
            throw std::invalid_argument(
                "SmartParallel Accurate parallel_transform_reduce supports floating-point std::plus only");
        }
    }

    detail::require_numerical_capability(
        detail::native_pairwise_reduction_capabilities, options.policy);
    T result = detail::canonical_transform_reduce(first,
                                                  total,
                                                  std::move(init),
                                                  std::move(binary_operation),
                                                  std::move(unary_operation),
                                                  callsite);
    detail::authenticate_numerical_execution("parallel_transform_reduce",
                                             options.policy,
                                             detail::AccumulationMethod::CanonicalPairwise,
                                             detail::canonical_pairwise_plan_v1,
                                             true, total != 0);
    return result;
}

template <typename InputIterator1,
          typename InputIterator2,
          typename T,
          typename BinaryOperation,
          typename BinaryTransform>
T parallel_transform_reduce(InputIterator1 first1,
                            InputIterator1 last1,
                            InputIterator2 first2,
                            T init,
                            BinaryOperation binary_operation,
                            BinaryTransform binary_transform,
                            NumericalOptions options)
{
    if (options.policy == NumericalPolicy::Fast)
    {
        const bool has_work = first1 != last1;
        T result = parallel_transform_reduce(first1,
                                             last1,
                                             first2,
                                             std::move(init),
                                             std::move(binary_operation),
                                             std::move(binary_transform));
        detail::authenticate_numerical_execution("parallel_transform_reduce_binary",
                                                 options.policy,
                                                 detail::AccumulationMethod::Native,
                                                 "none", true, has_work);
        return result;
    }

    static_assert(detail::is_random_access_iterator_v<InputIterator2>,
                  "SmartParallel parallel_transform_reduce requires random-access iterators");
    const std::size_t total = detail::algorithm_range_size(
        first1, last1, "parallel_transform_reduce");
    const std::size_t callsite = detail::combine_hash(
        detail::algorithm_identity(detail::ParallelAlgorithmKind::TransformReduce,
                                   binary_operation,
                                   binary_transform),
        static_cast<std::size_t>(options.policy));
    if (options.policy == NumericalPolicy::Accurate)
    {
        if constexpr (std::is_floating_point_v<T>
                      && detail::is_std_plus_v<BinaryOperation>
                      && detail::is_std_multiplies_v<BinaryTransform>)
        {
            detail::require_numerical_capability(
                detail::native_compensated_reduction_capabilities, options.policy);
            auto product = [first1, first2](std::size_t index) -> T
            {
                return static_cast<T>(*(first1 + static_cast<std::ptrdiff_t>(index)))
                    * static_cast<T>(*(first2 + static_cast<std::ptrdiff_t>(index)));
            };
            detail::CompensatedState<T> initial;
            detail::compensated_add(initial, init);
            if (total == 0)
            {
                T result = detail::finish_compensated(initial);
                detail::authenticate_numerical_execution(
                    "parallel_transform_reduce_binary", options.policy,
                    detail::AccumulationMethod::Compensated,
                    detail::compensated_plan_v1, true, false);
                return result;
            }
            const std::size_t leaves = detail::canonical_leaf_count(total);
            std::vector<std::optional<detail::CompensatedState<T>>> partials(leaves);
            detail::execute_canonical_leaves(total, callsite,
                [&](std::size_t leaf, detail::AlgorithmChunkRange range)
                {
                    detail::CompensatedState<T> state;
                    for (std::size_t index = range.begin; index < range.end; ++index)
                        detail::compensated_add(state, product(index));
                    partials[leaf].emplace(state);
                });
            auto state = detail::canonical_merge_tree(partials,
                                                      detail::merge_compensated<T>);
            state = detail::merge_compensated(initial, state);
            T result = detail::finish_compensated(state);
            detail::authenticate_numerical_execution("parallel_transform_reduce_binary",
                                                     options.policy,
                                                     detail::AccumulationMethod::Compensated,
                                                     detail::compensated_plan_v1);
            return result;
        }
        throw std::invalid_argument(
            "SmartParallel Accurate binary transform_reduce supports floating-point plus/multiplies only");
    }

    // Reproducible binary transform-reduce uses an explicit indexed canonical tree.
    detail::require_numerical_capability(
        detail::native_pairwise_reduction_capabilities, options.policy);
    if (total == 0)
    {
        detail::authenticate_numerical_execution(
            "parallel_transform_reduce_binary", options.policy,
            detail::AccumulationMethod::CanonicalPairwise,
            detail::canonical_pairwise_plan_v1, true, false);
        return init;
    }
    const std::size_t leaves = detail::canonical_leaf_count(total);
    std::vector<std::optional<T>> partials(leaves);
    detail::execute_canonical_leaves(total, callsite,
        [&](std::size_t leaf, detail::AlgorithmChunkRange range)
        {
            BinaryOperation local_binary = binary_operation;
            BinaryTransform local_transform = binary_transform;
            T partial = static_cast<T>(std::invoke(
                local_transform,
                *(first1 + static_cast<std::ptrdiff_t>(range.begin)),
                *(first2 + static_cast<std::ptrdiff_t>(range.begin))));
            for (std::size_t index = range.begin + 1; index < range.end; ++index)
                partial = std::invoke(
                    local_binary,
                    std::move(partial),
                    static_cast<T>(std::invoke(
                        local_transform,
                        *(first1 + static_cast<std::ptrdiff_t>(index)),
                        *(first2 + static_cast<std::ptrdiff_t>(index)))));
            partials[leaf].emplace(std::move(partial));
        });
    T result = detail::canonical_merge_tree(partials, binary_operation);
    result = std::invoke(binary_operation, std::move(init), std::move(result));
    detail::authenticate_numerical_execution("parallel_transform_reduce_binary",
                                             options.policy,
                                             detail::AccumulationMethod::CanonicalPairwise,
                                             detail::canonical_pairwise_plan_v1);
    return result;
}
} // namespace smart
