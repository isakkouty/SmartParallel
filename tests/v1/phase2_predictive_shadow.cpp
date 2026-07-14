#include <smart/core/config.hpp>
#include <smart/decision/decision.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_builder.hpp>

#include <cassert>
#include <vector>

int main()
{
    std::vector<int> values(100'000, 1);
    const smart::Workload workload =
        smart::WorkloadBuilder::container(values);
    const smart::WorkloadAnalysis analysis =
        smart::WorkloadAnalyzer().analyze(workload);

    smart::FunctionProfile profile;
    profile.available = true;
    profile.metadata.source = smart::ObservationSource::Sampled;
    profile.metadata.confidence = smart::ObservationConfidence::High;
    profile.measurement_reliable = true;
    profile.stop_reason = smart::ProfileStopReason::ConfidenceReached;
    profile.measured_batches = 12;
    profile.estimated_total_work_ms = 100.0;
    profile.estimated_parallel_overhead_ms = 0.20;
    profile.steady_state_ms_per_iteration = 0.001;
    profile.trimmed_mean_ms_per_iteration = 0.001;
    profile.median_ms_per_iteration = 0.001;
    profile.avg_ms_per_iteration = 0.001;
    profile.coefficient_of_variation = 0.05;
    profile.tail_ratio = 1.05;
    profile.regional_cost_ratio = 1.05;

    smart::global_config().enable_predictive_shadow = true;
    smart::global_config().enable_predictive_decisions = false;

    smart::DecisionEngine engine;
    const smart::ExecutionPlan selected =
        engine.decide(workload, analysis, &profile);
    const smart::DecisionReport& shadow_report = engine.last_report();

    assert(shadow_report.predictive_model_available);
    assert(shadow_report.predictive_shadow_mode);
    assert(!shadow_report.predictive_plan_applied);
    assert(!shadow_report.predictive_candidates.empty());

    smart::global_config().enable_predictive_decisions = true;
    smart::global_config().minimum_predictive_confidence = 0.50;

    const smart::ExecutionPlan predictive_selected =
        engine.decide(workload, analysis, &profile);
    const smart::DecisionReport& control_report = engine.last_report();

    assert(control_report.predictive_model_available);
    assert(!control_report.predictive_shadow_mode);
    assert(control_report.predictive_plan_applied);
    assert(control_report.source == smart::DecisionSource::Predictive);
    assert(
        predictive_selected.strategy ==
        control_report.predictive_plan.strategy);
    assert(
        predictive_selected.engine ==
        control_report.predictive_plan.engine);

    smart::global_config().enable_predictive_decisions = false;
    (void)selected;
    return 0;
}
