#pragma once

#include <smart/core/config.hpp>
#include <smart/decision/decision_report.hpp>
#include <smart/model/execution_characteristics_builder.hpp>
#include <smart/model/performance_model.hpp>
#include <smart/execution/runtime_capabilities.hpp>
#include <limits>
#include <smart/workload/workload_analyzer.hpp>

namespace smart
{
class EngineSelector
{
  public:
    DecisionReport score(const WorkloadAnalysis& analysis) const
    {
        ExecutionHints hints;
        return score(analysis, hints);
    }

    DecisionReport score(const WorkloadAnalysis& analysis, const ExecutionHints& hints) const
    {
        PerformanceModelBuilder model_builder;
        PerformanceModel model = model_builder.build(analysis, hints);

        ExecutionCharacteristicsBuilder execution_builder;
        ExecutionCharacteristics execution = execution_builder.build(model);

        DecisionReport report;
        report.analysis = analysis;
        report.model = model;
        report.execution = execution;

        // Base scores
        report.thread_pool_score = 0.2;
        report.static_thread_score = 0.0;
        report.one_tbb_score = 0.0;

        if (execution.scheduling == SchedulingPreference::Static)
        {
            report.static_thread_score += 0.8;
            report.thread_pool_score += 0.3;
            report.one_tbb_score += 1.4;
        }
        if (execution.memory_locality_critical)
        {
            report.static_thread_score += 0.5;
            report.thread_pool_score += 0.3;
        }

        if (execution.scheduling == SchedulingPreference::Dynamic)
        {
            report.one_tbb_score += 2.0;
            report.thread_pool_score += 0.4;
            report.static_thread_score += 0.3;
        }

        if (!execution_backend_available(ExecutionEngineType::OneTbb))
            report.one_tbb_score = -std::numeric_limits<double>::infinity();

        if (report.static_thread_score >= report.one_tbb_score
            && report.static_thread_score >= report.thread_pool_score)
        {
            report.plan.engine = ExecutionEngineType::StaticThread;
        }
        else if (report.one_tbb_score >= report.thread_pool_score)
        {
            report.plan.engine = ExecutionEngineType::OneTbb;
        }
        else
        {
            report.plan.engine = ExecutionEngineType::ThreadPool;
        }

        return report;
    }
};
} // namespace smart
