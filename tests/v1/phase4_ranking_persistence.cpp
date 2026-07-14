#include <smart/core/config.hpp>
#include <smart/decision/candidate_ranker.hpp>
#include <smart/decision/plan_prediction.hpp>
#include <smart/experience/experience_database.hpp>
#include <smart/workload/fingerprint.hpp>

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

int main()
{
    const bool old_ranking = smart::global_config().enable_experience_ranking;
    const std::size_t old_minimum =
        smart::global_config().minimum_ranking_samples;
    const double old_weight =
        smart::global_config().maximum_ranking_history_weight;

    smart::global_config().enable_experience_ranking = true;
    smart::global_config().minimum_ranking_samples = 3;
    smart::global_config().maximum_ranking_history_weight = 0.90;

    smart::ExperienceDatabase& database =
        smart::global_experience_database();
    database.clear();

    smart::WorkloadFingerprint fingerprint;
    fingerprint.value = 0x42u;

    smart::ExecutionPlan slower;
    slower.parallel = true;
    slower.engine = smart::ExecutionEngineType::ThreadPool;
    slower.strategy = smart::ExecutionStrategy::DynamicChunks;
    slower.job_count = 8;
    slower.chunk_size = 64;

    smart::ExecutionPlan faster = slower;
    faster.engine = smart::ExecutionEngineType::OneTbb;

    for (int i = 0; i < 6; ++i)
    {
        database.record(fingerprint, slower, 15.0 + i * 0.01);
        database.record(fingerprint, faster, 8.0 + i * 0.01);
    }

    const std::string path = "phase4_ranking_persistence_test.db";
    assert(database.save_to_file(path));
    database.clear();
    assert(database.load_from_file(path));

    std::vector<smart::PlanCostEstimate> candidates(2);
    candidates[0].available = true;
    candidates[0].plan = slower;
    candidates[0].predicted_total_ms = 10.0;

    candidates[1].available = true;
    candidates[1].plan = faster;
    candidates[1].predicted_total_ms = 11.0;

    smart::CandidateRanker().rank(candidates, fingerprint);

    assert(candidates[0].experience_rank_used);
    assert(candidates[1].experience_rank_used);
    assert(candidates[1].ranking_score < candidates[0].ranking_score);
    assert(candidates[1].ranking_samples >= 6);

    database.clear();
    std::remove(path.c_str());

    smart::global_config().enable_experience_ranking = old_ranking;
    smart::global_config().minimum_ranking_samples = old_minimum;
    smart::global_config().maximum_ranking_history_weight = old_weight;
    return 0;
}
