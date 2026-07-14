#include <smart/core/config.hpp>
#include <smart/decision/candidate_ranker.hpp>
#include <smart/experience/experience_database.hpp>

#include <algorithm>
#include <cassert>
#include <vector>

namespace
{
    smart::ExecutionPlan plan(
        smart::ExecutionEngineType engine,
        std::size_t workers)
    {
        smart::ExecutionPlan value;
        value.parallel = true;
        value.engine = engine;
        value.strategy = smart::ExecutionStrategy::DynamicChunks;
        value.job_count = workers;
        value.chunk_size = 64;
        return value;
    }

    smart::PlanCostEstimate candidate(
        const smart::ExecutionPlan& execution_plan,
        double predicted_ms)
    {
        smart::PlanCostEstimate value;
        value.available = true;
        value.plan = execution_plan;
        value.predicted_total_ms = predicted_ms;
        value.confidence = 0.5;
        return value;
    }
}

int main()
{
    smart::ExperienceDatabase& database =
        smart::global_experience_database();
    database.clear();

    const smart::WorkloadFingerprint fingerprint{0x12345678u};
    const smart::ExecutionPlan thread_pool = plan(
        smart::ExecutionEngineType::ThreadPool,
        8);
    const smart::ExecutionPlan one_tbb = plan(
        smart::ExecutionEngineType::OneTbb,
        8);

    std::vector<smart::PlanCostEstimate> candidates;
    candidates.push_back(candidate(thread_pool, 9.0));
    candidates.push_back(candidate(one_tbb, 10.0));

    const bool old_ranking =
        smart::global_config().enable_experience_ranking;
    const std::size_t old_minimum =
        smart::global_config().minimum_ranking_samples;
    const double old_weight =
        smart::global_config().maximum_ranking_history_weight;

    smart::global_config().enable_experience_ranking = true;
    smart::global_config().minimum_ranking_samples = 3;
    smart::global_config().maximum_ranking_history_weight = 0.75;

    for (int sample = 0; sample < 30; ++sample)
    {
        database.record(fingerprint, thread_pool, 14.0);
        database.record(fingerprint, one_tbb, 8.0);
    }

    smart::CandidateRanker().rank(candidates, fingerprint);

    assert(candidates[0].experience_rank_used);
    assert(candidates[1].experience_rank_used);
    assert(candidates[1].ranking_score < candidates[0].ranking_score);
    assert(candidates[1].ranking_samples == 30);
    assert(candidates[1].ranking_history_weight > 0.0);

    database.clear();
    smart::global_config().enable_experience_ranking = old_ranking;
    smart::global_config().minimum_ranking_samples = old_minimum;
    smart::global_config().maximum_ranking_history_weight = old_weight;
    return 0;
}
