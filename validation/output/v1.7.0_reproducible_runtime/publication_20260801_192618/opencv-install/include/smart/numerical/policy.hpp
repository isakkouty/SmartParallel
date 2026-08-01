#pragma once

#include <cstddef>
#include <smart/core/config.hpp>

namespace smart
{
enum class NumericalPolicy
{
    Fast,
    Reproducible,
    Accurate
};

struct NumericalOptions
{
    NumericalPolicy policy = NumericalPolicy::Fast;
};

namespace detail
{
enum class EvaluationOrder
{
    Adaptive,
    CanonicalDeterministic,
    CanonicalPointwise
};

enum class AccumulationMethod
{
    Native,
    FixedPointwiseExpression,
    CanonicalPairwise,
    Compensated,
    ScaledSumOfSquares
};
}

struct NumericalExecutionReport
{
    const char* operation = "unknown";
    NumericalPolicy policy = NumericalPolicy::Fast;
    detail::EvaluationOrder evaluation_order = detail::EvaluationOrder::Adaptive;
    detail::AccumulationMethod accumulation = detail::AccumulationMethod::Native;
    const char* canonical_plan = "none";
    ExecutionEngineType requested_engine = ExecutionEngineType::Auto;
    ExecutionEngineType selected_engine = ExecutionEngineType::Auto;
    const char* scheduler = "Unknown";
    bool parallel = false;
    std::size_t requested_worker_budget = 1;
    std::size_t worker_count = 1;
    bool route_authenticated = false;
    bool capability_satisfied = true;
};

inline NumericalExecutionReport& global_last_numerical_execution_report()
{
    static thread_local NumericalExecutionReport report;
    return report;
}

inline const char* numerical_policy_name(NumericalPolicy policy) noexcept
{
    switch (policy)
    {
        case NumericalPolicy::Fast: return "Fast";
        case NumericalPolicy::Reproducible: return "Reproducible";
        case NumericalPolicy::Accurate: return "Accurate";
        default: return "Unknown";
    }
}
} // namespace smart
