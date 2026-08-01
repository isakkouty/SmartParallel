#pragma once

#include <smart/data/view.hpp>
#include <smart/execution/algorithms.hpp>
#include <smart/numerical/detail/canonical_pointwise.hpp>

#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace smart::linalg
{
namespace detail_linalg
{
template <typename T>
void require_vector_pair(const smart::data::VectorView<const T>& x,
                         const smart::data::VectorView<const T>& y,
                         const char* operation)
{
    if (x.extent(0) != y.extent(0))
        throw std::invalid_argument(std::string("SmartParallel ") + operation
                                    + " requires equal vector extents");
}

template <typename State, typename Leaf, typename Merge>
State adaptive_reduce(std::size_t total,
                      std::size_t callsite,
                      Leaf leaf_function,
                      Merge merge)
{
    if (total == 0)
        return State{};
    const std::size_t chunks = smart::detail::algorithm_chunk_count(total);
    std::vector<std::optional<State>> partials(chunks);
    auto chunk = [&](std::size_t ordinal)
    {
        partials[ordinal].emplace(leaf_function(
            smart::detail::algorithm_chunk_range(total, chunks, ordinal)));
    };
    auto sequential = [&](std::size_t begin, std::size_t end)
    {
        for (std::size_t ordinal = begin; ordinal < end; ++ordinal)
            chunk(ordinal);
    };
    smart::detail::execute_algorithm_chunks(
        chunks, callsite,
        smart::detail::AlgorithmChunkFunctionRef(chunk),
        smart::detail::AlgorithmSequentialRangeFunctionRef(sequential));
    State result = std::move(partials[0].value());
    for (std::size_t index = 1; index < chunks; ++index)
        result = merge(std::move(result), std::move(partials[index].value()));
    return result;
}

template <typename T>
T native_dot(const smart::data::VectorView<const T>& x,
             const smart::data::VectorView<const T>& y)
{
    const std::size_t total = x.extent(0);
    if (total == 0) return T{0};

    const T* const x_data = x.data();
    const T* const y_data = y.data();
    const std::size_t x_stride = x.stride(0);
    const std::size_t y_stride = y.stride(0);
    auto leaf = [=](smart::detail::AlgorithmChunkRange range)
    {
        T partial = T{0};
        if (x_stride == 1 && y_stride == 1)
        {
            for (std::size_t index = range.begin; index < range.end; ++index)
                partial += x_data[index] * y_data[index];
        }
        else
        {
            const T* x_pointer = x_data + range.begin * x_stride;
            const T* y_pointer = y_data + range.begin * y_stride;
            for (std::size_t index = range.begin; index < range.end; ++index)
            {
                partial += *x_pointer * *y_pointer;
                x_pointer += x_stride;
                y_pointer += y_stride;
            }
        }
        return partial;
    };
    return adaptive_reduce<T>(total, 0xD0711001u, leaf,
                              [](T left, T right) { return left + right; });
}
}

template <typename T>
void axpy(smart::data::VectorView<T> y,
          T alpha,
          smart::data::VectorView<const T> x,
          NumericalOptions options = {})
{
    static_assert(std::is_floating_point_v<T>, "SmartParallel AXPY supports floating point");
    smart::detail::require_numerical_capability(
        smart::detail::native_pointwise_capabilities, options.policy);
    if (x.extent(0) != y.extent(0))
        throw std::invalid_argument("SmartParallel axpy requires equal vector extents");
    if (!y.has_unique_mapping())
        throw std::invalid_argument("SmartParallel axpy requires uniquely mapped output elements");
    const auto overlap = y.overlap(x);
    if (overlap == smart::data::OverlapKind::Overlap
        || overlap == smart::data::OverlapKind::Unknown)
        throw std::invalid_argument("SmartParallel axpy rejects partial or ambiguous overlap");
    const std::size_t total = x.extent(0);
    const T* const x_data = x.data();
    T* const y_data = y.data();
    const std::size_t x_stride = x.stride(0);
    const std::size_t y_stride = y.stride(0);
    auto apply_range = [=](smart::detail::AlgorithmChunkRange range)
    {
        if (x_stride == 1 && y_stride == 1)
        {
            for (std::size_t index = range.begin; index < range.end; ++index)
                y_data[index] = alpha * x_data[index] + y_data[index];
        }
        else
        {
            const T* x_pointer = x_data + range.begin * x_stride;
            T* y_pointer = y_data + range.begin * y_stride;
            for (std::size_t index = range.begin; index < range.end; ++index)
            {
                *y_pointer = alpha * *x_pointer + *y_pointer;
                x_pointer += x_stride;
                y_pointer += y_stride;
            }
        }
    };
    if (options.policy == NumericalPolicy::Fast)
    {
        smart::detail::run_chunked_algorithm(
            total, 0xA8F10000u,
            apply_range,
            [&] { apply_range({0, total}); });
        smart::detail::authenticate_numerical_execution(
            "axpy", options.policy, smart::detail::AccumulationMethod::Native, "none",
            true, total != 0);
    }
    else
    {
        smart::detail::execute_canonical_pointwise(
            total, 0xA8F10001u, apply_range);
        smart::detail::authenticate_numerical_execution(
            "axpy", options.policy,
            smart::detail::AccumulationMethod::FixedPointwiseExpression,
            smart::detail::canonical_pointwise_plan_v1, true, total != 0);
    }
}

template <typename T>
T dot(smart::data::VectorView<const T> x,
      smart::data::VectorView<const T> y,
      NumericalOptions options = {})
{
    static_assert(std::is_floating_point_v<T>, "SmartParallel dot supports floating point");
    detail_linalg::require_vector_pair(x, y, "dot");
    const std::size_t total = x.extent(0);
    const T* const x_data = x.data();
    const T* const y_data = y.data();
    const std::size_t x_stride = x.stride(0);
    const std::size_t y_stride = y.stride(0);
    if (options.policy == NumericalPolicy::Fast)
    {
        T result = detail_linalg::native_dot(x, y);
        smart::detail::authenticate_numerical_execution(
            "dot", options.policy, smart::detail::AccumulationMethod::Native, "none",
            true, total != 0);
        return result;
    }

    const std::size_t leaves = smart::detail::canonical_leaf_count(total);
    if (total == 0)
    {
        smart::detail::authenticate_numerical_execution(
            "dot", options.policy,
            options.policy == NumericalPolicy::Accurate
                ? smart::detail::AccumulationMethod::Compensated
                : smart::detail::AccumulationMethod::CanonicalPairwise,
            options.policy == NumericalPolicy::Accurate
                ? smart::detail::compensated_plan_v1
                : smart::detail::canonical_pairwise_plan_v1,
            true, false);
        return T{0};
    }

    if (options.policy == NumericalPolicy::Accurate)
    {
        smart::detail::require_numerical_capability(
            smart::detail::native_compensated_reduction_capabilities, options.policy);
        std::vector<std::optional<smart::detail::CompensatedState<T>>> partials(leaves);
        smart::detail::execute_canonical_leaves(total, 0xD0711003u,
            [&](std::size_t leaf, smart::detail::AlgorithmChunkRange range)
            {
                smart::detail::CompensatedState<T> state;
                const T* x_pointer = x_data + range.begin * x_stride;
                const T* y_pointer = y_data + range.begin * y_stride;
                for (std::size_t index = range.begin; index < range.end; ++index)
                {
                    smart::detail::compensated_add(state, *x_pointer * *y_pointer);
                    x_pointer += x_stride;
                    y_pointer += y_stride;
                }
                partials[leaf].emplace(state);
            });
        auto state = smart::detail::canonical_merge_tree(
            partials, smart::detail::merge_compensated<T>);
        T result = smart::detail::finish_compensated(state);
        smart::detail::authenticate_numerical_execution(
            "dot", options.policy, smart::detail::AccumulationMethod::Compensated,
            smart::detail::compensated_plan_v1);
        return result;
    }

    smart::detail::require_numerical_capability(
        smart::detail::native_pairwise_reduction_capabilities, options.policy);
    std::vector<std::optional<T>> partials(leaves);
    smart::detail::execute_canonical_leaves(total, 0xD0711002u,
        [&](std::size_t leaf, smart::detail::AlgorithmChunkRange range)
        {
            T partial = T{0};
            const T* x_pointer = x_data + range.begin * x_stride;
            const T* y_pointer = y_data + range.begin * y_stride;
            for (std::size_t index = range.begin; index < range.end; ++index)
            {
                partial += *x_pointer * *y_pointer;
                x_pointer += x_stride;
                y_pointer += y_stride;
            }
            partials[leaf].emplace(partial);
        });
    T result = smart::detail::canonical_merge_tree(
        partials, [](T left, T right) { return left + right; });
    smart::detail::authenticate_numerical_execution(
        "dot", options.policy, smart::detail::AccumulationMethod::CanonicalPairwise,
        smart::detail::canonical_pairwise_plan_v1);
    return result;
}

template <typename T>
T norm(smart::data::VectorView<const T> x,
       NumericalOptions options = {})
{
    static_assert(std::is_floating_point_v<T>, "SmartParallel norm supports floating point");
    const std::size_t total = x.extent(0);
    const T* const x_data = x.data();
    const std::size_t x_stride = x.stride(0);
    if (options.policy == NumericalPolicy::Accurate)
    {
        smart::detail::require_numerical_capability(
            smart::detail::native_scaled_sumsq_capabilities, options.policy);
        if (total == 0)
        {
            smart::detail::authenticate_numerical_execution(
                "norm", options.policy, smart::detail::AccumulationMethod::ScaledSumOfSquares,
                smart::detail::scaled_sumsq_plan_v1, true, false);
            return T{0};
        }
        const std::size_t leaves = smart::detail::canonical_leaf_count(total);
        std::vector<std::optional<smart::detail::ScaledSumSquaresState<T>>> partials(leaves);
        smart::detail::execute_canonical_leaves(total, 0xB0F10003u,
            [&](std::size_t leaf, smart::detail::AlgorithmChunkRange range)
            {
                smart::detail::ScaledSumSquaresState<T> state;
                const T* x_pointer = x_data + range.begin * x_stride;
                for (std::size_t index = range.begin; index < range.end; ++index)
                {
                    smart::detail::scaled_sumsq_add(state, *x_pointer);
                    x_pointer += x_stride;
                }
                partials[leaf].emplace(state);
            });
        auto state = smart::detail::canonical_merge_tree(
            partials, smart::detail::merge_scaled_sumsq<T>);
        T result = smart::detail::finish_scaled_sumsq(state);
        smart::detail::authenticate_numerical_execution(
            "norm", options.policy, smart::detail::AccumulationMethod::ScaledSumOfSquares,
            smart::detail::scaled_sumsq_plan_v1);
        return result;
    }

    if (options.policy == NumericalPolicy::Reproducible)
        smart::detail::require_numerical_capability(
            smart::detail::native_pairwise_reduction_capabilities, options.policy);
    T squared = dot<T>(x, x, options);
    T result = std::sqrt(squared);
    smart::detail::authenticate_numerical_execution(
        "norm", options.policy,
        options.policy == NumericalPolicy::Fast
            ? smart::detail::AccumulationMethod::Native
            : smart::detail::AccumulationMethod::CanonicalPairwise,
        options.policy == NumericalPolicy::Fast ? "none"
                                                : smart::detail::canonical_pairwise_plan_v1);
    return result;
}
} // namespace smart::linalg
