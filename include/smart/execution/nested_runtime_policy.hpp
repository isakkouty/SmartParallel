#pragma once

#include <smart/decision/execution_plan.hpp>
#include <smart/execution/backend.hpp>
#include <smart/execution/execution_context.hpp>

namespace smart
{
inline ExecutionEngineType resolve_execution_engine_type(ExecutionEngineType type) noexcept
{
    return execution_engine(type).type();
}

inline NestedExecutionPolicy select_nested_execution_policy(const ExecutionContext& parent,
                                                              const ExecutionPlan& child_plan)
{
    if (parent.depth == 0 || !parent.parallel || !child_plan.parallel
        || child_plan.strategy == ExecutionStrategy::Sequential)
    {
        return NestedExecutionPolicy::NotNested;
    }

    const ExecutionEngineType child_engine = resolve_execution_engine_type(child_plan.engine);
    if (parent.engine == child_engine
        && execution_engine(child_engine).capabilities().supports_native_nesting)
    {
        return NestedExecutionPolicy::NativeRuntimeDelegation;
    }

    return NestedExecutionPolicy::SequentialFallback;
}

inline NestedExecutionPolicy apply_nested_execution_policy(const ExecutionContext& parent,
                                                             ExecutionPlan& child_plan)
{
    const NestedExecutionPolicy policy = select_nested_execution_policy(parent, child_plan);
    if (policy == NestedExecutionPolicy::SequentialFallback)
    {
        child_plan.parallel = false;
        child_plan.strategy = ExecutionStrategy::Sequential;
        child_plan.job_count = 1;
        child_plan.chunk_size = 0;
    }
    return policy;
}
} // namespace smart
