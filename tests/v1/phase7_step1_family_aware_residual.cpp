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

    smart::PlanCostEstimate estimate(smart::WorkloadFamily family)
    {
        smart::PlanCostEstimate value;
        value.available = true;
        value.plan = plan();
        value.predicted_total_ms = 100.0;
        value.predicted_execution_ms = 90.0;
        value.scheduling_overhead_ms = 5.0;
        value.framework_overhead_ms = 5.0;
        value.confidence = 0.5;
        value.workload_family = family;
        value.workload_family_confidence = 1.0;
        return value;
    }
}

int main()
{
    const smart::Config saved = smart::global_config();
    smart::Config& config = smart::global_config();
    config.enable_residual_correction = true;
    config.enable_residual_similarity_transfer = false;
    config.enable_family_aware_residual_correction = true;
    config.minimum_residual_correction_samples = 3;
    config.residual_full_confidence_samples = 4;
    config.maximum_residual_correction_weight = 0.65;

    smart::ExperienceDatabase& database = smart::global_experience_database();
    database.clear();

    smart::WorkloadFingerprint fingerprint;
    fingerprint.value = 0xA551u;

    for (int i = 0; i < 4; ++i)
        database.record(fingerprint, plan(), 140.0, 100.0);

    smart::ResidualCorrectionPolicy policy;
    const smart::ResidualCorrectionResult compute = policy.evaluate(
        estimate(smart::WorkloadFamily::ComputeHeavy),
        fingerprint);
    const smart::ResidualCorrectionResult streaming = policy.evaluate(
        estimate(smart::WorkloadFamily::StreamingMemory),
        fingerprint);
    const smart::ResidualCorrectionResult unknown = policy.evaluate(
        estimate(smart::WorkloadFamily::Unknown),
        fingerprint);

    assert(compute.applied);
    assert(streaming.applied);
    assert(compute.family_aware);
    assert(streaming.family_aware);

    // Identical history must be used more conservatively for streaming work,
    // where timing is more sensitive to contention and external bandwidth.
    assert(streaming.history_weight < compute.history_weight);
    assert(streaming.factor < compute.factor);
    assert(streaming.effective_maximum_factor <
           compute.effective_maximum_factor);

    // Unknown families remain conservative even when the caller reports a
    // confident Unknown classification.
    assert(unknown.family_aware);
    assert(unknown.factor >= 1.0);
    assert(unknown.factor < compute.factor);

    // Log-space blending remains bounded and never applies the full 1.4x
    // observation after only four samples.
    assert(compute.factor > 1.0);
    assert(compute.factor < 1.28);
    assert(streaming.factor <= 1.16 + 1e-12);

    database.clear();
    smart::global_config() = saved;
    return 0;
}
