#pragma once

#include <smart/model/execution_characteristics.hpp>
#include <smart/model/performance_model.hpp>

namespace smart
{
class ExecutionCharacteristicsBuilder
{
  public:
    ExecutionCharacteristics build(const PerformanceModel& model) const
    {
        ExecutionCharacteristics execution;

        if (model.workload.has_few_iterations)
        {
            execution.scheduling = SchedulingPreference::Sequential;
            execution.scheduler_overhead_sensitive = true;
            return execution;
        }

        if (model.function.available && model.function.arithmetic_intensity > 0.5)
        {
            execution.scheduling = SchedulingPreference::Dynamic;
            execution.load_balancing_important = true;
            return execution;
        }

        if (model.l3_pressure >= 1.0)
        {
            execution.scheduling = SchedulingPreference::Static;
            execution.memory_locality_critical = true;
            return execution;
        }

        execution.scheduling = SchedulingPreference::Static;

        return execution;
    }
};
} // namespace smart
