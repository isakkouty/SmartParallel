#pragma once

#include <smart/execution/nested_execution_coordinator.hpp>

namespace smart
{
inline NestedExecutionPolicy select_nested_execution_policy(const ExecutionContext& parent,
                                                              const ExecutionPlan& child_plan)
{
    return NestedExecutionCoordinator{}.coordinate(parent, child_plan).policy;
}

inline NestedExecutionPolicy apply_nested_execution_policy(const ExecutionContext& parent,
                                                             ExecutionPlan& child_plan)
{
    const NestedExecutionDecision decision =
        NestedExecutionCoordinator{}.coordinate(parent, child_plan);
    child_plan = decision.plan;
    return decision.policy;
}
} // namespace smart
