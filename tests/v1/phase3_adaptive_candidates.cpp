#include <smart/core/config.hpp>
#include <smart/decision/predictive_decision_model.hpp>
#include <smart/hardware/hardware.hpp>
#include <smart/profiling/function_profiler.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_builder.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <set>
#include <vector>

int main()
{
    const smart::Config saved = smart::global_config();
    smart::global_config().enable_machine_runtime_calibration = false;
    smart::global_config().enable_prediction_calibration = false;
    smart::global_config().enable_adaptive_execution_candidates = true;

    std::vector<int> values(1'000'000, 1);
    const smart::Workload workload =
        smart::WorkloadBuilder::container(values);
    const smart::WorkloadAnalysis analysis =
        smart::WorkloadAnalyzer().analyze(workload);

    smart::FunctionProfile profile;
    profile.available = true;
    profile.measurement_reliable = true;
    profile.metadata.confidence = smart::ObservationConfidence::High;
    profile.measured_batches = 12;
    profile.steady_state_ms_per_iteration = 0.001;
    profile.trimmed_mean_ms_per_iteration = 0.001;
    profile.median_ms_per_iteration = 0.001;
    profile.avg_ms_per_iteration = 0.001;
    profile.estimated_total_work_ms = 1'000.0;
    profile.estimated_parallel_overhead_ms = 0.05;
    profile.coefficient_of_variation = 0.10;
    profile.tail_ratio = 1.10;
    profile.regional_cost_ratio = 1.0;

    const smart::PredictiveDecisionResult adaptive =
        smart::PredictiveDecisionModel().predict(
            workload,
            analysis,
            &profile);

    assert(adaptive.available);
    assert(!adaptive.candidates.empty());

    std::set<std::size_t> worker_counts;
    bool saw_dynamic_chunk = false;
    for (const smart::PlanCostEstimate& candidate : adaptive.candidates)
    {
        if (!candidate.plan.parallel)
            continue;

        worker_counts.insert(candidate.plan.job_count);
        assert(candidate.plan.job_count >= 2);
        assert(candidate.plan.job_count <= smart::hardware_threads());

        if (candidate.plan.strategy == smart::ExecutionStrategy::DynamicChunks)
        {
            assert(candidate.plan.chunk_size >= 1);
            saw_dynamic_chunk = true;
        }
        else
        {
            assert(candidate.plan.chunk_size == 0);
        }
    }

    assert(saw_dynamic_chunk);
    if (smart::hardware_threads() >= 4)
        assert(worker_counts.size() >= 2);

    smart::global_config().enable_adaptive_execution_candidates = false;
    const smart::PredictiveDecisionResult fixed =
        smart::PredictiveDecisionModel().predict(
            workload,
            analysis,
            &profile);

    assert(fixed.available);
    assert(fixed.candidates.size() <= 4);

    smart::global_config() = saved;
    return 0;
}
