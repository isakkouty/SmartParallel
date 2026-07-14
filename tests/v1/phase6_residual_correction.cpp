#include <smart/core/config.hpp>
#include <smart/decision/residual_correction.hpp>
#include <smart/experience/experience_database.hpp>

#include <cassert>
#include <cmath>

namespace
{
    smart::ExecutionPlan plan()
    {
        smart::ExecutionPlan value;
        value.parallel = true;
        value.engine = smart::ExecutionEngineType::OneTbb;
        value.strategy = smart::ExecutionStrategy::DynamicChunks;
        value.job_count = 8;
        value.chunk_size = 64;
        return value;
    }

    smart::PlanCostEstimate estimate()
    {
        smart::PlanCostEstimate value;
        value.available = true;
        value.plan = plan();
        value.predicted_total_ms = 100.0;
        value.predicted_execution_ms = 90.0;
        value.scheduling_overhead_ms = 5.0;
        value.framework_overhead_ms = 5.0;
        value.confidence = 0.5;
        return value;
    }
}

int main()
{
    smart::Config saved = smart::global_config();
    smart::global_config().enable_residual_correction = true;
    smart::global_config().enable_residual_similarity_transfer = false;
    smart::global_config().minimum_residual_correction_samples = 3;
    smart::global_config().residual_full_confidence_samples = 4;
    smart::global_config().maximum_residual_correction_weight = 0.65;

    smart::ExperienceDatabase& database = smart::global_experience_database();
    database.clear();

    smart::WorkloadFingerprint fingerprint;
    fingerprint.value = 1234;

    for (int i = 0; i < 4; ++i)
        database.record(fingerprint, plan(), 120.0, 100.0);

    smart::PlanCostEstimate corrected = estimate();
    smart::ResidualCorrectionPolicy policy;
    const smart::ResidualCorrectionResult stable =
        policy.evaluate(corrected, fingerprint);

    assert(stable.applied);
    assert(stable.exact_history_used);
    assert(stable.samples == 4);
    assert(stable.factor > 1.0);
    assert(stable.factor < 1.20); // bounded blend, not full replacement
    assert(stable.uncertainty < 0.1);

    policy.apply(corrected, fingerprint);
    assert(corrected.residual_correction_applied);
    assert(corrected.pre_residual_total_ms == 100.0);
    assert(corrected.predicted_total_ms > 100.0);
    assert(corrected.predicted_total_ms < 120.0);

    database.clear();
    smart::PlanCostEstimate cold = estimate();
    const smart::ResidualCorrectionResult no_history =
        policy.evaluate(cold, fingerprint);
    assert(!no_history.applied);
    assert(std::abs(no_history.factor - 1.0) < 1e-12);

    smart::global_config() = saved;
    return 0;
}
