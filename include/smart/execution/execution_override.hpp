#pragma once

#include <smart/decision/execution_plan.hpp>

namespace smart::detail
{
inline const ExecutionPlan*& active_forced_execution_plan() noexcept
{
    static thread_local const ExecutionPlan* plan = nullptr;
    return plan;
}

class ForcedExecutionPlanScope
{
  public:
    explicit ForcedExecutionPlanScope(const ExecutionPlan* plan) noexcept
        : previous_(active_forced_execution_plan())
    {
        active_forced_execution_plan() = plan;
    }

    ~ForcedExecutionPlanScope()
    {
        active_forced_execution_plan() = previous_;
    }

    ForcedExecutionPlanScope(const ForcedExecutionPlanScope&) = delete;
    ForcedExecutionPlanScope& operator=(const ForcedExecutionPlanScope&) = delete;

  private:
    const ExecutionPlan* previous_ = nullptr;
};

class ForcedExecutionPlanSuspendScope
{
  public:
    ForcedExecutionPlanSuspendScope() noexcept
        : previous_(active_forced_execution_plan())
    {
        active_forced_execution_plan() = nullptr;
    }

    ~ForcedExecutionPlanSuspendScope()
    {
        active_forced_execution_plan() = previous_;
    }

  private:
    const ExecutionPlan* previous_ = nullptr;
};
}
