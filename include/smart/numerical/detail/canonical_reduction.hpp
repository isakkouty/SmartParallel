#pragma once

#include <smart/execution/algorithms.hpp>
#include <smart/hardware/hardware.hpp>
#include <smart/numerical/capability.hpp>
#include <smart/numerical/policy.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace smart::detail
{
inline constexpr std::size_t canonical_reduction_leaf_size = 1024;
inline constexpr const char* canonical_pairwise_plan_v1 = "canonical-pairwise-v1-leaf1024";
inline constexpr const char* compensated_plan_v1 = "canonical-neumaier-v1-leaf1024";
inline constexpr const char* scaled_sumsq_plan_v1 = "canonical-scaled-sumsq-v1-leaf1024";

inline std::size_t canonical_leaf_count(std::size_t total)
{
    if (total == 0)
        return 0;
    return 1 + (total - 1) / canonical_reduction_leaf_size;
}

inline AlgorithmChunkRange canonical_leaf_range(std::size_t total, std::size_t leaf) noexcept
{
    const std::size_t begin = leaf * canonical_reduction_leaf_size;
    const std::size_t remaining = total - begin;
    const std::size_t end = remaining <= canonical_reduction_leaf_size
        ? total
        : begin + canonical_reduction_leaf_size;
    return {begin, end};
}

inline void authenticate_numerical_execution(const char* operation,
                                             NumericalPolicy policy,
                                             AccumulationMethod method,
                                             const char* plan,
                                             bool capability = true,
                                             bool work_executed = true)
{
    auto& report = global_last_numerical_execution_report();
    report.operation = operation;
    report.policy = policy;
    report.evaluation_order = policy == NumericalPolicy::Fast
        ? EvaluationOrder::Adaptive
        : method == AccumulationMethod::FixedPointwiseExpression
            ? EvaluationOrder::CanonicalPointwise
            : EvaluationOrder::CanonicalDeterministic;
    report.accumulation = method;
    report.canonical_plan = plan;
    report.requested_engine = global_config().execution_engine;
    report.requested_worker_budget = global_config().nested_root_concurrency_budget == 0
        ? hardware_threads() : global_config().nested_root_concurrency_budget;
    if (work_executed)
    {
        const auto& decision = global_last_decision_report();
        report.parallel = decision.plan.parallel;
        report.selected_engine = decision.plan.engine;
        report.scheduler = decision.plan.parallel ? engine_name(decision.plan.engine) : "Sequential";
        report.worker_count = decision.plan.parallel
            ? std::max<std::size_t>(1, decision.plan.job_count) : 1;
        const bool engine_matches = !decision.plan.parallel
            || report.requested_engine == ExecutionEngineType::Auto
            || decision.plan.engine == report.requested_engine;
        const bool budget_respected = report.worker_count
            <= std::max<std::size_t>(std::size_t{1}, report.requested_worker_budget);
        report.route_authenticated = engine_matches && budget_respected;
    }
    else
    {
        report.parallel = false;
        report.selected_engine = ExecutionEngineType::Auto;
        report.scheduler = "Sequential(no-work)";
        report.worker_count = 1;
        report.route_authenticated = true;
    }
    report.capability_satisfied = capability;
}

template <typename LeafFunction>
void execute_canonical_leaves(std::size_t total,
                              std::size_t callsite,
                              LeafFunction&& leaf_function)
{
    const std::size_t leaves = canonical_leaf_count(total);
    if (leaves == 0)
        return;
    auto chunk = [&](std::size_t leaf) { leaf_function(leaf, canonical_leaf_range(total, leaf)); };
    auto sequential = [&](std::size_t begin, std::size_t end)
    {
        for (std::size_t leaf = begin; leaf < end; ++leaf)
            chunk(leaf);
    };
    execute_algorithm_chunks(leaves,
                             callsite,
                             AlgorithmChunkFunctionRef(chunk),
                             AlgorithmSequentialRangeFunctionRef(sequential));
}

template <typename State, typename Merge>
State canonical_merge_tree(std::vector<std::optional<State>>& states, Merge merge)
{
    if (states.empty())
        throw std::logic_error("SmartParallel canonical merge requires at least one state");
    for (std::size_t width = 1; width < states.size();)
    {
        const std::size_t step = width > std::numeric_limits<std::size_t>::max() / 2
            ? std::numeric_limits<std::size_t>::max()
            : width * 2;
        for (std::size_t left = 0; left < states.size(); left += step)
        {
            if (width < states.size() - left)
            {
                const std::size_t right = left + width;
                states[left] = merge(std::move(states[left].value()),
                                     std::move(states[right].value()));
            }
        }
        if (width > states.size() / 2)
            break;
        width *= 2;
    }
    return std::move(states.front().value());
}

template <typename InputIterator, typename T, typename BinaryOperation, typename Transform>
T canonical_transform_reduce(InputIterator first,
                             std::size_t total,
                             T init,
                             BinaryOperation binary_operation,
                             Transform transform,
                             std::size_t callsite)
{
    if (total == 0)
        return init;
    const std::size_t leaves = canonical_leaf_count(total);
    std::vector<std::optional<T>> partials(leaves);
    execute_canonical_leaves(total, callsite, [&](std::size_t leaf, AlgorithmChunkRange range)
    {
        BinaryOperation local_binary = binary_operation;
        Transform local_transform = transform;
        T partial = std::invoke(local_transform, *algorithm_advance(first, range.begin));
        for (std::size_t index = range.begin + 1; index < range.end; ++index)
            partial = std::invoke(local_binary,
                                  std::move(partial),
                                  std::invoke(local_transform, *algorithm_advance(first, index)));
        partials[leaf].emplace(std::move(partial));
    });
    T result = canonical_merge_tree(partials, binary_operation);
    return std::invoke(binary_operation, std::move(init), std::move(result));
}

template <typename T>
struct CompensatedState
{
    T sum = T{0};
    T correction = T{0};
    bool nan = false;
    bool positive_infinity = false;
    bool negative_infinity = false;
};

template <typename T>
void compensated_add(CompensatedState<T>& state, T value)
{
    static_assert(std::is_floating_point_v<T>, "compensated_add requires floating point");
    if (std::isnan(value))
    {
        state.nan = true;
        return;
    }
    if (std::isinf(value))
    {
        if (std::signbit(value)) state.negative_infinity = true;
        else state.positive_infinity = true;
        return;
    }
    if (state.nan || state.positive_infinity || state.negative_infinity)
        return;
    const T next = state.sum + value;
    if (std::isinf(next))
    {
        if (std::signbit(next)) state.negative_infinity = true;
        else state.positive_infinity = true;
        return;
    }
    if (std::abs(state.sum) >= std::abs(value))
        state.correction += (state.sum - next) + value;
    else
        state.correction += (value - next) + state.sum;
    state.sum = next;
}

template <typename T>
CompensatedState<T> merge_compensated(CompensatedState<T> left,
                                      const CompensatedState<T>& right)
{
    left.nan = left.nan || right.nan;
    left.positive_infinity = left.positive_infinity || right.positive_infinity;
    left.negative_infinity = left.negative_infinity || right.negative_infinity;
    if (!left.nan && !(left.positive_infinity && left.negative_infinity))
    {
        compensated_add(left, right.sum);
        compensated_add(left, right.correction);
    }
    return left;
}

template <typename T>
T finish_compensated(const CompensatedState<T>& state)
{
    if (state.nan || (state.positive_infinity && state.negative_infinity))
        return std::numeric_limits<T>::quiet_NaN();
    if (state.positive_infinity)
        return std::numeric_limits<T>::infinity();
    if (state.negative_infinity)
        return -std::numeric_limits<T>::infinity();
    return state.sum + state.correction;
}

template <typename InputIterator, typename T, typename Transform>
T canonical_compensated_reduce(InputIterator first,
                               std::size_t total,
                               T init,
                               Transform transform,
                               std::size_t callsite)
{
    CompensatedState<T> initial;
    compensated_add(initial, init);
    if (total == 0)
        return finish_compensated(initial);
    const std::size_t leaves = canonical_leaf_count(total);
    std::vector<std::optional<CompensatedState<T>>> partials(leaves);
    execute_canonical_leaves(total, callsite, [&](std::size_t leaf, AlgorithmChunkRange range)
    {
        CompensatedState<T> state;
        Transform local_transform = transform;
        for (std::size_t index = range.begin; index < range.end; ++index)
            compensated_add(state, static_cast<T>(std::invoke(local_transform,
                                                              *algorithm_advance(first, index))));
        partials[leaf].emplace(state);
    });
    auto state = canonical_merge_tree(partials, merge_compensated<T>);
    state = merge_compensated(initial, state);
    return finish_compensated(state);
}

template <typename T>
struct ScaledSumSquaresState
{
    T scale = T{0};
    T sumsq = T{1};
    bool has_finite = false;
    bool nan = false;
    bool infinity = false;
};

template <typename T>
void scaled_sumsq_add(ScaledSumSquaresState<T>& state, T value)
{
    const T absolute = std::abs(value);
    if (std::isnan(absolute)) { state.nan = true; return; }
    if (std::isinf(absolute)) { state.infinity = true; return; }
    if (absolute == T{0}) return;
    state.has_finite = true;
    if (state.scale < absolute)
    {
        const T ratio = state.scale == T{0} ? T{0} : state.scale / absolute;
        state.sumsq = T{1} + state.sumsq * ratio * ratio;
        state.scale = absolute;
    }
    else
    {
        const T ratio = absolute / state.scale;
        state.sumsq += ratio * ratio;
    }
}

template <typename T>
ScaledSumSquaresState<T> merge_scaled_sumsq(ScaledSumSquaresState<T> left,
                                            const ScaledSumSquaresState<T>& right)
{
    left.nan = left.nan || right.nan;
    left.infinity = left.infinity || right.infinity;
    if (!right.has_finite) return left;
    if (!left.has_finite)
    {
        auto merged = right;
        merged.nan = merged.nan || left.nan;
        merged.infinity = merged.infinity || left.infinity;
        return merged;
    }
    if (left.scale < right.scale)
    {
        const T ratio = left.scale / right.scale;
        left.sumsq = right.sumsq + left.sumsq * ratio * ratio;
        left.scale = right.scale;
    }
    else
    {
        const T ratio = right.scale / left.scale;
        left.sumsq += right.sumsq * ratio * ratio;
    }
    left.has_finite = true;
    return left;
}

template <typename T>
T finish_scaled_sumsq(const ScaledSumSquaresState<T>& state)
{
    if (state.nan) return std::numeric_limits<T>::quiet_NaN();
    if (state.infinity) return std::numeric_limits<T>::infinity();
    if (!state.has_finite) return T{0};
    return state.scale * std::sqrt(state.sumsq);
}
} // namespace smart::detail
