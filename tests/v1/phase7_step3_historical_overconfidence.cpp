#include <smart/core/config.hpp>
#include <smart/decision/candidate_ranker.hpp>
#include <smart/experience/experience_database.hpp>

#include <cassert>
#include <cstddef>
#include <vector>

namespace
{
    smart::ExecutionPlan make_plan(
        smart::ExecutionEngineType engine,
        std::size_t workers)
    {
        smart::ExecutionPlan plan;
        plan.parallel = true;
        plan.engine = engine;
        plan.strategy = smart::ExecutionStrategy::DynamicChunks;
        plan.job_count = workers;
        plan.chunk_size = 64;
        return plan;
    }

    smart::PlanCostEstimate estimate(
        const smart::ExecutionPlan& plan,
        double predicted)
    {
        smart::PlanCostEstimate value;
        value.available = true;
        value.plan = plan;
        value.predicted_total_ms = predicted;
        value.confidence = 0.5;
        return value;
    }

    smart::WorkloadFingerprint fingerprint(std::size_t value)
    {
        smart::WorkloadFingerprint fp;
        fp.value = value;
        fp.kind_bucket = 1;
        fp.iteration_bucket = 131072;
        fp.working_set_bucket = 524288;
        fp.object_size_bucket = 4;
        fp.function_cost_bucket = 32780;
        fp.variation_bucket = 32784;
        return fp;
    }
}

int main()
{
    smart::Config& config = smart::global_config();
    const smart::Config old_config = config;

    config.enable_experience_ranking = true;
    config.enable_similarity_transfer = false;
    config.minimum_ranking_samples = 3;
    config.maximum_ranking_history_weight = 0.90;
    config.enable_historical_overconfidence_control = true;
    config.ranking_full_confidence_samples = 24;
    config.ranking_recent_regret_soft_limit_percent = 10.0;
    config.ranking_recent_disagreement_scale_percent = 20.0;

    smart::ExperienceDatabase& database =
        smart::global_experience_database();
    database.clear();

    const smart::WorkloadFingerprint fp = fingerprint(700);
    const smart::ExecutionPlan thread_pool = make_plan(
        smart::ExecutionEngineType::ThreadPool, 8);
    const smart::ExecutionPlan one_tbb = make_plan(
        smart::ExecutionEngineType::OneTbb, 8);

    // Sparse history must not immediately receive dominant authority.
    for (int i = 0; i < 3; ++i)
    {
        database.record_outcome(fp, thread_pool, 9.0, 9.0, 10.0);
        database.record_outcome(fp, one_tbb, 10.0, 9.0, 9.5);
    }

    std::vector<smart::PlanCostEstimate> candidates;
    candidates.push_back(estimate(thread_pool, 10.0));
    candidates.push_back(estimate(one_tbb, 9.5));
    smart::CandidateRanker().rank(candidates, fp);

    assert(candidates[0].historical_overconfidence_control_applied);
    assert(candidates[0].historical_effective_weight > 0.0);
    assert(candidates[0].historical_effective_weight < 0.60);
    assert(candidates[0].historical_evidence_confidence < 0.50);

    const double sparse_weight =
        candidates[0].historical_effective_weight;

    // Stable, repeated low-regret evidence earns more authority gradually.
    for (int i = 0; i < 24; ++i)
    {
        database.record_outcome(fp, thread_pool, 9.0, 9.0, 10.0);
        database.record_outcome(fp, one_tbb, 10.0, 9.0, 9.5);
    }

    candidates[0] = estimate(thread_pool, 10.0);
    candidates[1] = estimate(one_tbb, 9.5);
    smart::CandidateRanker().rank(candidates, fp);

    const double mature_weight =
        candidates[0].historical_effective_weight;
    assert(mature_weight > sparse_weight);
    assert(mature_weight <= config.maximum_ranking_history_weight);
    assert(candidates[0].historical_stability_confidence > 0.90);

    // One strongly contradictory recent result must immediately reduce the
    // authority of previously successful history instead of letting it stay
    // stubbornly dominant.
    database.record_outcome(fp, thread_pool, 20.0, 9.0, 10.0);

    candidates[0] = estimate(thread_pool, 10.0);
    candidates[1] = estimate(one_tbb, 9.5);
    smart::CandidateRanker().rank(candidates, fp);

    assert(candidates[0].historical_effective_weight < mature_weight);
    assert(candidates[0].historical_recent_consistency < 0.50);
    assert(candidates[0].historical_effective_weight <=
           config.maximum_ranking_history_weight);

    database.clear();
    config = old_config;
    return 0;
}
