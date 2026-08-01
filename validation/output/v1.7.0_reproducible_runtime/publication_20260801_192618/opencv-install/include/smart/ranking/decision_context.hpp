#pragma once

#include <cstddef>
#include <smart/decision/execution_hints.hpp>
#include <smart/profiling/function_profiler.hpp>

namespace smart::ranking
{
// Production-available workload context for utility ranking.
// It deliberately contains no predicted runtime or measured candidate runtime.
struct DecisionContext
{
    std::size_t logical_iterations = 0;

    bool profile_available = false;
    double profile_median_ms_per_iteration = 0.0;
    double profile_coefficient_of_variation = 0.0;
    double profile_tail_ratio = 0.0;

    double profile_parallel_worthiness = 0.0;

    double profile_regional_cost_ratio = 0.0;
    ExecutionHints hints{};
};

inline DecisionContext make_decision_context(std::size_t logical_iterations,
                                             const FunctionProfile* profile,
                                             const ExecutionHints* hints = nullptr)
{
    DecisionContext context;
    context.logical_iterations = logical_iterations;
    if (profile != nullptr && profile->available)
    {
        context.profile_available = true;
        context.profile_median_ms_per_iteration = profile->median_ms_per_iteration;
        context.profile_coefficient_of_variation = profile->coefficient_of_variation;
        context.profile_tail_ratio = profile->tail_ratio;
        context.profile_parallel_worthiness = profile->parallel_worthiness;
        context.profile_regional_cost_ratio = profile->regional_cost_ratio;
    }
    if (hints != nullptr)
        context.hints = *hints;
    return context;
}
} // namespace smart::ranking
