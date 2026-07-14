#include <smart/core/config.hpp>
#include <smart/decision/candidate_ranker.hpp>
#include <smart/experience/experience_database.hpp>

#include <cassert>
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
    config.minimum_ranking_samples = 3;
    config.maximum_ranking_history_weight = 0.90;
    config.ranking_history_decay = 0.80;
    config.enable_similarity_transfer = true;
    config.maximum_similarity_history_weight = 0.20;
    config.minimum_similarity = 0.50;

    smart::ExperienceDatabase& database =
        smart::global_experience_database();
    database.clear();

    const smart::ExecutionPlan thread_pool = make_plan(
        smart::ExecutionEngineType::ThreadPool, 8);
    const smart::ExecutionPlan one_tbb = make_plan(
        smart::ExecutionEngineType::OneTbb, 8);
    const smart::WorkloadFingerprint exact = fingerprint(100);

    // Analytical prediction prefers ThreadPool, but measured outcomes show
    // that oneTBB repeatedly has lower regret.
    for (int i = 0; i < 8; ++i)
    {
        database.record_outcome(exact, thread_pool, 14.0, 8.0, 9.0);
        database.record_outcome(exact, one_tbb, 8.0, 8.0, 10.0);
    }

    std::vector<smart::PlanCostEstimate> candidates;
    candidates.push_back(estimate(thread_pool, 9.0));
    candidates.push_back(estimate(one_tbb, 10.0));
    smart::CandidateRanker().rank(candidates, exact);

    assert(candidates[1].ranking_score < candidates[0].ranking_score);
    assert(candidates[1].ranking_success_rate >
           candidates[0].ranking_success_rate);
    assert(candidates[0].ranking_regret_percent > 0.0);

    // Recent reversed outcomes must eventually override stale evidence.
    for (int i = 0; i < 24; ++i)
    {
        database.record_outcome(exact, thread_pool, 7.0, 7.0, 9.0);
        database.record_outcome(exact, one_tbb, 12.0, 7.0, 10.0);
    }

    candidates[0] = estimate(thread_pool, 9.0);
    candidates[1] = estimate(one_tbb, 10.0);
    smart::CandidateRanker().rank(candidates, exact);
    assert(candidates[0].ranking_score < candidates[1].ranking_score);

    // Similar workloads can receive a bounded transfer signal without being
    // treated as an exact match.
    smart::WorkloadFingerprint similar = fingerprint(200);
    similar.iteration_bucket *= 2;
    std::vector<smart::PlanCostEstimate> transfer_candidates;
    transfer_candidates.push_back(estimate(thread_pool, 10.0));
    transfer_candidates.push_back(estimate(one_tbb, 10.0));
    smart::CandidateRanker().rank(transfer_candidates, similar);
    assert(transfer_candidates[0].similarity_rank_used ||
           transfer_candidates[1].similarity_rank_used);
    assert(transfer_candidates[0].ranking_similarity >=
           config.minimum_similarity);

    database.clear();
    config = old_config;
    return 0;
}
