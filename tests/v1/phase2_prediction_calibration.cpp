#include <smart/core/config.hpp>
#include <smart/decision/predictive_decision_model.hpp>
#include <smart/experience/experience_database.hpp>
#include <smart/workload/fingerprint.hpp>
#include <smart/workload/workload_analyzer.hpp>
#include <smart/workload/workload_builder.hpp>

#include <cassert>
#include <vector>

int main()
{
    std::vector<int> values(100000, 1);
    const smart::Workload workload =
        smart::WorkloadBuilder::container(values);
    const smart::WorkloadAnalysis analysis =
        smart::WorkloadAnalyzer().analyze(workload);

    smart::FunctionProfile profile;
    profile.available = true;
    profile.measurement_reliable = true;
    profile.metadata.confidence = smart::ObservationConfidence::High;
    profile.measured_batches = 8;
    profile.trimmed_mean_ms_per_iteration = 0.0005;
    profile.steady_state_ms_per_iteration = 0.0005;
    profile.estimated_total_work_ms = 50.0;
    profile.estimated_parallel_overhead_ms = 0.1;
    profile.tail_ratio = 1.0;
    profile.regional_cost_ratio = 1.0;

    smart::global_experience_database().clear();
    smart::global_config().enable_prediction_calibration = false;

    const smart::PredictiveDecisionResult base =
        smart::PredictiveDecisionModel().predict(
            workload,
            analysis,
            &profile);

    assert(base.available);

    const smart::WorkloadFingerprint fp =
        smart::fingerprint(workload, &profile);

    for (const smart::PlanCostEstimate& candidate : base.candidates)
    {
        const double predicted_runtime =
            candidate.predicted_total_ms -
            candidate.framework_overhead_ms;

        for (int sample = 0; sample < 6; ++sample)
        {
            smart::global_experience_database().record(
                fp,
                candidate.plan,
                predicted_runtime * 1.5,
                predicted_runtime);
        }
    }

    smart::global_config().enable_prediction_calibration = true;
    smart::global_config().minimum_calibration_samples = 3;
    smart::global_config().minimum_calibration_confidence = 0.0;

    const smart::PredictiveDecisionResult calibrated =
        smart::PredictiveDecisionModel().predict(
            workload,
            analysis,
            &profile);

    assert(calibrated.available);
    assert(calibrated.candidates.size() == base.candidates.size());

    for (std::size_t i = 0; i < calibrated.candidates.size(); ++i)
    {
        assert(calibrated.candidates[i].calibration_applied);
        assert(calibrated.candidates[i].calibration_factor > 1.0);
        assert(
            calibrated.candidates[i].predicted_total_ms >
            base.candidates[i].predicted_total_ms);
    }

    smart::global_experience_database().clear();
    return 0;
}
