#include <smart/decision/composite_decision_provider.hpp>
#include <smart/decision/decision.hpp>
#include <smart/decision/decision_report.hpp>
#include <smart/decision/decision_rules.hpp>
#include <smart/hardware/hardware.hpp>
#include <smart/ranking/runtime_utility_policy.hpp>
#include <smart/workload/workload_analyzer.hpp>

namespace smart
{

ExecutionPlan DecisionEngine::decide(const Workload& workload,
                                     const WorkloadAnalysis& analysis,
                                     const FunctionProfile* function_profile)
{
    ExecutionHints hints;
    return decide(workload, analysis, hints, function_profile);
}

ExecutionPlan DecisionEngine::decide(const Workload& workload,
                                     const WorkloadAnalysis& analysis,
                                     const ExecutionHints& hints,
                                     const FunctionProfile* function_profile)
{
    DecisionContext context{workload, analysis, function_profile};

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

    ranking::RuntimeUtilityPolicy utility_policy;
    const auto utility = utility_policy.choose(context, hints, last_report_);
    last_report_.utility_model_loaded = utility.model_loaded;
    last_report_.utility_model_promoted = utility.model_promoted;
    last_report_.utility_model_compatible = utility.model_compatible;
    last_report_.utility_model_applied = utility.applied;
    last_report_.utility_model_confidence = utility.confidence;
    last_report_.utility_model_reason = utility.reason;
    if (utility.applied)
    {
        last_report_.plan = utility.plan;
        last_report_.source = DecisionSource::Predictive;
        last_report_.decision_confidence = utility.confidence;
    }

    return last_report_.plan;
}
} // namespace smart
