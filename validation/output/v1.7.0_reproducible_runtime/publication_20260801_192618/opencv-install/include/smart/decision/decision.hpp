#pragma once

#include <cstddef>
#include <smart/core/config.hpp>
#include <smart/decision/decision_report.hpp>
#include <smart/decision/execution_hints.hpp>
#include <smart/decision/execution_plan.hpp>
#include <smart/profiling/function_profiler.hpp>
#include <smart/workload/workload.hpp>
#include <vector>

namespace smart
{
inline DecisionReport& global_last_decision_report()
{
    static thread_local DecisionReport report;
    return report;
}

struct WorkloadAnalysis;

class DecisionEngine
{
  public:
    ExecutionPlan decide(const Workload& workload,
                         const WorkloadAnalysis& analysis,
                         const FunctionProfile* function_profile = nullptr);

    ExecutionPlan decide(const Workload& workload,
                         const WorkloadAnalysis& analysis,
                         const ExecutionHints& hints,
                         const FunctionProfile* function_profile = nullptr);

    const DecisionReport& last_report() const
    {
        return last_report_;
    }

    const ExecutionPlan& last_plan() const
    {
        return last_report_.plan;
    }

  private:
    DecisionReport last_report_;
};

} // namespace smart
