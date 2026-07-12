#pragma once

#include <smart/decision/decision_source.hpp>
#include <smart/decision/execution_plan.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/model/performance_model.hpp>
#include <smart/model/execution_characteristics.hpp>

namespace smart
{
    struct DecisionReport
    {
        PerformanceModel model;

        WorkloadAnalysis analysis;
        ExecutionPlan plan;

        ExecutionCharacteristics execution;

        double thread_pool_score = 0.0;
        double static_thread_score = 0.0;
        double one_tbb_score = 0.0;

        DecisionSource source = DecisionSource::Analytical;

        double decision_confidence = 1.0;
    };
}
