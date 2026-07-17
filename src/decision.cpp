#include <smart/decision/decision.hpp>
#include <smart/hardware/hardware.hpp>
#include <smart/decision/decision_rules.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/decision/decision_report.hpp>
#include <smart/decision/composite_decision_provider.hpp>

namespace smart
{

    ExecutionPlan DecisionEngine::decide(
        const Workload& workload,
        const WorkloadAnalysis& analysis,
        const FunctionProfile* function_profile)
    {
        ExecutionHints hints;
        return decide(workload, analysis, hints, function_profile);
    }

    ExecutionPlan DecisionEngine::decide(
        const Workload& workload,
        const WorkloadAnalysis& analysis,
        const ExecutionHints& hints,
        const FunctionProfile* function_profile)
    {
        DecisionContext context{ workload, analysis, function_profile };

        CompositeDecisionProvider provider;

        auto report = provider.decide(context, hints);

        if (!report)
        {
            last_report_ = DecisionReport{};
            last_report_.analysis = analysis;
            if (function_profile != nullptr)
            {
                last_report_.has_function_profile = true;
                last_report_.function_profile = *function_profile;
            }
            last_report_.plan.parallel = false;
            last_report_.plan.strategy = ExecutionStrategy::Sequential;
            last_report_.plan.job_count = 1;

            return last_report_.plan;
        }

        last_report_ = *report;
        if (function_profile != nullptr)
        {
            last_report_.has_function_profile = true;
            last_report_.function_profile = *function_profile;
        }

        return last_report_.plan;
    }
}
