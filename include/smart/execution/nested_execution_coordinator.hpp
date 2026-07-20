#pragma once

#include <algorithm>
#include <cstddef>
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
    std::size_t parent_budget = 1;
    std::size_t requested_budget = 1;
    std::size_t effective_budget = 1;

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
        decision.parent_budget = normalized_budget(parent.concurrency_budget);
        decision.requested_budget = requested_plan.parallel
                                        ? normalized_budget(requested_plan.job_count)
                                        : 1;

        if (decision.policy == NestedExecutionPolicy::NativeRuntimeDelegation)
        {
            decision.effective_budget =
                std::min(decision.parent_budget, decision.requested_budget);
            decision.plan.job_count = decision.effective_budget;
        }
        else if (decision.policy == NestedExecutionPolicy::SequentialFallback)
        {
            decision.plan.parallel = false;
            decision.plan.strategy = ExecutionStrategy::Sequential;
            decision.plan.job_count = 1;
            decision.plan.chunk_size = 0;
            decision.effective_budget = 1;
        }
        else
        {
            decision.effective_budget = decision.plan.parallel
                                            ? decision.requested_budget
                                            : 1;
            decision.plan.job_count = decision.effective_budget;
        }

        return decision;
    }

  private:
    static std::size_t normalized_budget(std::size_t budget) noexcept
    {
        return std::max<std::size_t>(1, budget);
    }

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
