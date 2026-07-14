#include <smart/decision/predictive_decision_model.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_builder.hpp>

#include <cassert>
#include <vector>

namespace
{
    smart::FunctionProfile make_profile(
        double total_work_ms,
        double per_iteration_ms,
        double cv,
        double tail_ratio,
        double regional_ratio)
    {
        smart::FunctionProfile profile;
        profile.available = true;
        profile.metadata.source = smart::ObservationSource::Sampled;
        profile.metadata.confidence = smart::ObservationConfidence::High;
        profile.measurement_reliable = true;
        profile.stop_reason = smart::ProfileStopReason::ConfidenceReached;
        profile.measured_batches = 12;
        profile.estimated_total_work_ms = total_work_ms;
        profile.estimated_parallel_overhead_ms = 0.20;
        profile.steady_state_ms_per_iteration = per_iteration_ms;
        profile.trimmed_mean_ms_per_iteration = per_iteration_ms;
        profile.median_ms_per_iteration = per_iteration_ms;
        profile.avg_ms_per_iteration = per_iteration_ms;
        profile.coefficient_of_variation = cv;
        profile.tail_ratio = tail_ratio;
        profile.regional_cost_ratio = regional_ratio;
        profile.profiling_elapsed_ms = 0.05;
        return profile;
    }
}

int main()
{
    std::vector<int> values(100'000, 1);
    const smart::Workload workload =
        smart::WorkloadBuilder::container(values);
    const smart::WorkloadAnalysis analysis =
        smart::WorkloadAnalyzer().analyze(workload);

    smart::PredictiveDecisionModel model;

    const smart::FunctionProfile cheap =
        make_profile(0.05, 0.0000005, 0.01, 1.0, 1.0);
    const smart::PredictiveDecisionResult cheap_prediction =
        model.predict(workload, analysis, &cheap);

    assert(cheap_prediction.available);
    assert(!cheap_prediction.recommended_plan.parallel);
    assert(
        cheap_prediction.recommended_plan.strategy ==
        smart::ExecutionStrategy::Sequential);

    const smart::FunctionProfile heavy =
        make_profile(120.0, 0.0012, 0.05, 1.05, 1.05);
    const smart::PredictiveDecisionResult heavy_prediction =
        model.predict(workload, analysis, &heavy);

    assert(heavy_prediction.available);
    assert(heavy_prediction.recommended_plan.parallel);
    assert(heavy_prediction.recommended_total_ms < 120.05);

    const smart::FunctionProfile irregular =
        make_profile(120.0, 0.0012, 1.25, 5.0, 4.0);
    const smart::PredictiveDecisionResult irregular_prediction =
        model.predict(workload, analysis, &irregular);

    assert(irregular_prediction.available);

    double one_tbb_total = 0.0;
    double static_total = 0.0;

    for (const smart::PlanCostEstimate& candidate :
         irregular_prediction.candidates)
    {
        if (candidate.plan.engine == smart::ExecutionEngineType::OneTbb &&
            candidate.plan.parallel)
        {
            one_tbb_total = candidate.predicted_total_ms;
        }

        if (candidate.plan.engine == smart::ExecutionEngineType::StaticThread &&
            candidate.plan.parallel)
        {
            static_total = candidate.predicted_total_ms;
        }
    }

    assert(one_tbb_total > 0.0);
    assert(static_total > 0.0);
    assert(one_tbb_total < static_total);

    return 0;
}
