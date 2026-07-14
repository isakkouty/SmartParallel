#include <smart/core/config.hpp>
#include <smart/decision/residual_correction.hpp>
#include <smart/experience/experience_database.hpp>
#include <smart/workload/fingerprint_similarity.hpp>

#include <cassert>
#include <cmath>

namespace
{
    smart::ExecutionPlan make_plan()
    {
        smart::ExecutionPlan plan;
        plan.parallel = true;
        plan.engine = smart::ExecutionEngineType::OneTbb;
        plan.strategy = smart::ExecutionStrategy::DynamicChunks;
        plan.job_count = 8;
        plan.chunk_size = 64;
        return plan;
    }

    smart::WorkloadFingerprint make_fingerprint(
        std::size_t value,
        std::size_t iterations,
        std::size_t bytes,
        std::size_t cost)
    {
        smart::WorkloadFingerprint fingerprint;
        fingerprint.value = value;
        fingerprint.kind_bucket = 1;
        fingerprint.iteration_bucket = iterations;
        fingerprint.working_set_bucket = bytes;
        fingerprint.object_size_bucket = 8;
        fingerprint.function_cost_bucket = cost;
        fingerprint.variation_bucket = 16;
        return fingerprint;
    }
}

int main()
{
    smart::Config& config = smart::global_config();
    config.enable_residual_correction = true;
    config.enable_residual_similarity_transfer = true;
    config.minimum_similarity = 0.55;
    config.maximum_residual_similarity_weight = 0.15;

    const smart::WorkloadFingerprint source =
        make_fingerprint(1001, 131072, 1048576, 32784);
    const smart::WorkloadFingerprint nearby =
        make_fingerprint(1002, 262144, 2097152, 32784);
    smart::WorkloadFingerprint distant = nearby;
    distant.value = 1003;
    distant.kind_bucket = 3;

    const smart::FingerprintSimilarity close =
        smart::compare_fingerprints(source, nearby);
    assert(close.compatible_kind);
    assert(close.total >= config.minimum_similarity);
    assert(smart::fingerprint_similarity(source, distant) == 0.0);

    smart::ExperienceDatabase& database =
        smart::global_experience_database();
    database.clear();

    const smart::ExecutionPlan plan = make_plan();
    for (int sample = 0; sample < 12; ++sample)
    {
        database.record_outcome(source, plan, 12.0, 10.0, 10.0);
    }

    smart::PlanCostEstimate estimate;
    estimate.available = true;
    estimate.plan = plan;
    estimate.predicted_total_ms = 10.0;
    estimate.predicted_execution_ms = 9.0;
    estimate.framework_overhead_ms = 1.0;

    const smart::ResidualCorrectionResult transferred =
        smart::ResidualCorrectionPolicy().evaluate(estimate, nearby);
    assert(transferred.similarity_history_used);
    assert(!transferred.exact_history_used);
    assert(transferred.similarity >= config.minimum_similarity);
    assert(transferred.history_weight > 0.0);
    assert(transferred.history_weight <=
        config.maximum_residual_similarity_weight + 1e-12);
    assert(transferred.factor >=
        config.minimum_similarity_residual_factor);
    assert(transferred.factor <=
        config.maximum_similarity_residual_factor);

    const smart::ResidualCorrectionResult rejected =
        smart::ResidualCorrectionPolicy().evaluate(estimate, distant);
    assert(!rejected.similarity_history_used);
    assert(!rejected.applied);

    database.clear();
    return 0;
}
