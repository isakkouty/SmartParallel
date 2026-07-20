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

struct NestedExecutionDecision
{
    NestedExecutionPolicy policy = NestedExecutionPolicy::NotNested;
    ExecutionPlan plan{};

    bool uses_sequential_fallback() const noexcept
    {
        return policy == NestedExecutionPolicy::SequentialFallback;
    }
};

class NestedExecutionCoordinator
{
  public:
    NestedExecutionDecision coordinate(const ExecutionContext& parent,
                                       const ExecutionPlan& requested_plan) const
    {
        NestedExecutionDecision decision;
        decision.plan = requested_plan;
        decision.policy = select_policy(parent, requested_plan);

        if (decision.policy == NestedExecutionPolicy::SequentialFallback)
        {
            decision.plan.parallel = false;
            decision.plan.strategy = ExecutionStrategy::Sequential;
            decision.plan.job_count = 1;
            decision.plan.chunk_size = 0;
        }

        return decision;
    }

  private:
    static NestedExecutionPolicy select_policy(const ExecutionContext& parent,
                                               const ExecutionPlan& child_plan)
    {
        if (parent.depth == 0 || !parent.parallel || !child_plan.parallel
            || child_plan.strategy == ExecutionStrategy::Sequential)
        {
            return NestedExecutionPolicy::NotNested;
        }

        const ExecutionEngineType child_engine =
            resolve_execution_engine_type(child_plan.engine);
        if (parent.engine == child_engine
            && execution_engine(child_engine).capabilities().supports_native_nesting)
        {
            return NestedExecutionPolicy::NativeRuntimeDelegation;
        }

        return NestedExecutionPolicy::SequentialFallback;
    }
};
} // namespace smart
