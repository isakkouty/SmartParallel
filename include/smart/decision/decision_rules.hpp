#pragma once

#include <optional>

#include <smart/core/config.hpp>

#include <smart/decision/decision.hpp>
#include <smart/decision/decision_source.hpp>
#include <smart/workload/fingerprint.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/decision/engine_selector.hpp>
#include <smart/hardware/hardware_characteristics.hpp>
#include <smart/profiling/function_profiler.hpp>


namespace smart
{
    struct DecisionContext
    {
        const Workload& workload;
        const WorkloadAnalysis& analysis;
        const FunctionProfile* function_profile = nullptr;
    };

    class SmallWorkloadRule
    {
    public:
        std::optional<DecisionReport> apply_report(
            const DecisionContext& context,
            const ExecutionHints& hints) const
        {
            if (!context.analysis.is_small)
                return std::nullopt;

            if (context.function_profile &&
                context.function_profile->available &&
                context.function_profile->parallel_worthiness >=
                global_config().parallel_for_minimum_predicted_speedup)
            {
                return std::nullopt;
            }

            EngineSelector selector;
            DecisionReport report = selector.score(context.analysis, hints);

            report.plan.strategy = ExecutionStrategy::Sequential;
            report.plan.parallel = false;
            report.plan.job_count = 1;
            report.source = DecisionSource::Analytical;
            report.decision_confidence = 0.75;

            return report;
        }

        std::optional<ExecutionPlan> apply(const DecisionContext& context) const
        {
            ExecutionHints hints;

            auto report = apply_report(context, hints);

            if (!report)
                return std::nullopt;

            return report->plan;
        }
    };

    class DefaultRule
    {
    public:

        DecisionReport apply_report(const DecisionContext& context) const;

        DecisionReport apply_report( const DecisionContext& context, const ExecutionHints& hints) const
        {
            EngineSelector selector;
            DecisionReport report = selector.score(context.analysis, hints);

            // Cheap contiguous workloads should not jump directly to StaticThread.
            // Benchmark evidence shows static thread creation overhead dominates here.

            if (!(context.function_profile &&
                context.function_profile->available &&
                context.function_profile->parallel_worthiness >=
                global_config().parallel_for_minimum_predicted_speedup) &&
                !report.model.likely_memory_sensitive &&
                context.analysis.working_set_bytes <= report.model.hardware.l3_cache_size &&
                !hints.available)
            {
                if (context.analysis.iterations <=
                    global_config().cheap_workload_sequential_threshold)
                {
                    report.plan.engine = ExecutionEngineType::ThreadPool;
                    report.plan.strategy = ExecutionStrategy::Sequential;
                    report.plan.parallel = false;
                    report.plan.job_count = 1;
                    report.source = DecisionSource::Analytical;
                    report.decision_confidence = 0.75;
                    return report;
                }
            }

            if (!(context.function_profile &&
                context.function_profile->available &&
                context.function_profile->parallel_worthiness >=
                global_config().parallel_for_minimum_predicted_speedup) &&
                report.execution.scheduling == SchedulingPreference::Sequential)
            {
                report.plan.strategy = ExecutionStrategy::Sequential;
                report.plan.parallel = false;
                report.plan.job_count = 1;
                report.source = DecisionSource::Analytical;
                report.decision_confidence = 0.75;
                return report;
            }

            report.plan.parallel = true;

            if (report.plan.engine == ExecutionEngineType::OneTbb)
            {
                report.plan.strategy = ExecutionStrategy::DynamicChunks;
            }
            else if (report.execution.scheduling == SchedulingPreference::Static)
            {
                report.plan.strategy = ExecutionStrategy::StaticChunks;
            }
            else if (report.execution.scheduling == SchedulingPreference::Dynamic)
            {
                report.plan.strategy = ExecutionStrategy::DynamicChunks;
            }
            else
            {
                report.plan.strategy = ExecutionStrategy::StaticChunks;
            }

            HardwareCharacteristics hw = hardware_characteristics();
            report.plan.job_count = hw.logical_threads;

            if (report.plan.job_count == 0)
            {
                report.plan.job_count = 1;
            }

            if (report.plan.job_count > context.workload.iterations)
            {
                report.plan.job_count = context.workload.iterations;
            }

            report.source = DecisionSource::Analytical;
            report.decision_confidence = 0.75;

            return report;
        }

        ExecutionPlan apply(const DecisionContext& context) const
        {
            return apply_report(context).plan;
        }
    };
}
