#pragma once

#include <algorithm>
#include <cstddef>
#include <smart/decision/execution_plan.hpp>
#include <smart/execution/backend.hpp>
#include <smart/execution/execution_context.hpp>
#include <smart/execution/nested_budget_partition.hpp>

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
    std::size_t allocated_budget = 1;
    std::size_t effective_budget = 1;
    std::size_t sibling_index = 0;
    std::size_t sibling_count = 1;

    bool uses_sequential_fallback() const noexcept
    {
        return policy == NestedExecutionPolicy::SequentialFallback;
    }

    bool uses_budget_limited_delegation() const noexcept
    {
        return policy == NestedExecutionPolicy::BudgetLimitedDelegation;
    }

    bool partition_exhausted() const noexcept
    {
        return allocated_budget == 0;
    }
};

class NestedExecutionCoordinator
{
  public:
    NestedExecutionDecision coordinate(const ExecutionContext& parent,
                                       const ExecutionPlan& requested_plan) const
    {
        const NestedBudgetPartition partition = NestedBudgetPartitioner{}.partition(
            parent.concurrency_budget, 1, 0);
        return coordinate(parent, requested_plan, partition);
    }

    NestedExecutionDecision coordinate(const ExecutionContext& parent,
                                       const ExecutionPlan& requested_plan,
                                       const NestedBudgetPartition& partition) const
    {
        NestedExecutionDecision decision;
        decision.plan = requested_plan;
        decision.parent_budget = normalized_budget(parent.concurrency_budget);
        decision.requested_budget = requested_plan.parallel
                                        ? normalized_budget(requested_plan.job_count)
                                        : 1;
        decision.sibling_index = partition.child_index;
        decision.sibling_count = std::max<std::size_t>(1, partition.child_count);
        decision.allocated_budget = std::min(decision.parent_budget, partition.allocated_budget);
        decision.policy = select_policy(parent,
                                        requested_plan,
                                        decision.allocated_budget,
                                        decision.requested_budget);

        if (decision.policy == NestedExecutionPolicy::NativeRuntimeDelegation)
        {
            decision.effective_budget = decision.requested_budget;
            decision.plan.job_count = decision.effective_budget;
        }
        else if (decision.policy == NestedExecutionPolicy::BudgetLimitedDelegation)
        {
            decision.effective_budget = decision.allocated_budget;
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
            decision.allocated_budget = decision.requested_budget;
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
                                               const ExecutionPlan& child_plan,
                                               std::size_t allocated_budget,
                                               std::size_t requested_budget)
    {
        if (parent.depth == 0 || !parent.parallel || !child_plan.parallel
            || child_plan.strategy == ExecutionStrategy::Sequential)
        {
            return NestedExecutionPolicy::NotNested;
        }

        const ExecutionEngineType child_engine =
            resolve_execution_engine_type(child_plan.engine);
        const bool compatible_native_runtime =
            parent.engine == child_engine
            && execution_engine(child_engine).capabilities().supports_native_nesting;

        if (!compatible_native_runtime || allocated_budget <= 1)
        {
            return NestedExecutionPolicy::SequentialFallback;
        }

        if (requested_budget > allocated_budget)
        {
            return NestedExecutionPolicy::BudgetLimitedDelegation;
        }

        return NestedExecutionPolicy::NativeRuntimeDelegation;
    }
};
} // namespace smart
